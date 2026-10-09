// 学習用: インタプリタの内部でいま何が起きているかを表示する。
//
//   make trace-run                      デフォルトの例をトレースする
//   ./trace "/sq { dup mul } def 7 sq"  任意のコードをトレースする
//
// 言語の評価を、途中経過を出しながらなぞる別実装。
// 配列・制御構造の表示はCの再帰で辿るため、本体の継続スタックやpcの
// 動きをそのまま表示するものではない。

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "parser.h"
#include "stack.h"
#include "dict.h"
#include "eval.h"
#include "element.h"
#include "exec_array.h"
#include "primitives.h"
#include "cont.h"

static CharSource src_from(const char *s) {
    CharSource src;

    src.fp = fmemopen((void *)s, strlen(s), "r");
    return src;
}

static void ind(int depth) {
    for (int i = 0; i < depth; i++) {
        printf("  ");
    }
}

// 1行に収まる短い表記。EXEC_ARRAY は中身を展開しない。
static void brief(const Element *e) {
    switch (e->type) {
    case ELEM_INT:          printf("INT %d", e->u.ival);                          break;
    case ELEM_LITERAL_NAME: printf("LITERAL_NAME \"%s\"", e->u.name);             break;
    case ELEM_EXEC_NAME:    printf("EXEC_NAME \"%s\"", e->u.name);                break;
    case ELEM_PRIMITIVE:    printf("PRIMITIVE <C関数>");                          break;
    case ELEM_EXEC_ARRAY:   printf("EXEC_ARRAY(count=%d)", e->u.exec_array->count); break;
    }
}

// 中身を展開して表示する。ネストした EXEC_ARRAY は段を下げて再帰表示。
static void detail(const Element *e, int depth) {
    brief(e);
    if (e->type != ELEM_EXEC_ARRAY) {
        return;
    }
    printf("\n");
    for (int i = 0; i < e->u.exec_array->count; i++) {
        ind(depth + 1);
        printf("items[%d] = ", i);
        detail(&e->u.exec_array->items[i], depth + 1);
        if (e->u.exec_array->items[i].type != ELEM_EXEC_ARRAY) {
            printf("\n");
        }
    }
}

// Stack は中身が隠されているので、全部 pop して表示し、積み直す。
static void dump_stack(Stack *st, int depth) {
    Element tmp[64];
    int n = stack_size(st);

    for (int i = n - 1; i >= 0; i--) {
        tmp[i] = stack_pop(st);
    }
    ind(depth);
    printf("スタック: ");
    if (n == 0) {
        printf("[空]");
    }
    for (int i = 0; i < n; i++) {
        printf("[ ");
        brief(&tmp[i]);
        printf(" ]");
    }
    printf("\n");
    for (int i = 0; i < n; i++) {
        stack_push(st, tmp[i]);
    }
}

static const char *token_type_name(TokenType t) {
    switch (t) {
    case TOKEN_INT:          return "INT";
    case TOKEN_EXEC_NAME:    return "EXEC_NAME";
    case TOKEN_LITERAL_NAME: return "LITERAL_NAME";
    case TOKEN_OPEN_BRACE:   return "OPEN_BRACE";
    case TOKEN_CLOSE_BRACE:  return "CLOSE_BRACE";
    default:                 return "EOF";
    }
}

static void trace_exec_array(ExecArray *ea, Stack *st, Dict *dict, int depth);

// if / ifelse / while は中身が実行可能配列なので、C関数をそのまま呼ぶと
// 内部が見えなくなる。ここだけ trace 側で同じ処理を書き直し、
// 中身も1ステップずつ追えるようにする。
// 扱った場合は true を返す（呼び出し側は C関数を呼ばない）。
static bool trace_control(const char *name, Stack *st, Dict *dict, int depth) {
    if (strcmp(name, "if") == 0) {
        Element proc = stack_pop(st);
        Element cond = stack_pop(st);

        ind(depth);
        printf("  if: 条件は %s\n", cond.u.ival ? "真 → ブロックを実行" : "偽 → 何もしない");
        if (cond.u.ival) {
            trace_exec_array(proc.u.exec_array, st, dict, depth + 2);
        }
        return true;
    }

    if (strcmp(name, "ifelse") == 0) {
        Element proc2 = stack_pop(st);
        Element proc1 = stack_pop(st);
        Element cond  = stack_pop(st);

        ind(depth);
        printf("  ifelse: 条件は %s\n", cond.u.ival ? "真 → 1つ目のブロック" : "偽 → 2つ目のブロック");
        trace_exec_array(cond.u.ival ? proc1.u.exec_array : proc2.u.exec_array,
                         st, dict, depth + 2);
        return true;
    }

    if (strcmp(name, "while") == 0) {
        Element body = stack_pop(st);
        Element cond = stack_pop(st);
        int round = 1;

        for (;;) {
            Element result;

            ind(depth);
            printf("  while [%d周目] 条件を評価\n", round);
            trace_exec_array(cond.u.exec_array, st, dict, depth + 2);
            result = stack_pop(st);

            ind(depth);
            printf("  while [%d周目] 条件は %s\n", round,
                   result.u.ival ? "真 → 本体を実行" : "偽 → ループ終了");
            if (!result.u.ival) {
                break;
            }
            trace_exec_array(body.u.exec_array, st, dict, depth + 2);
            round++;
        }
        return true;
    }

    return false;
}

// eval_element と同じことを、途中経過を出しながらやる
static void trace_element(Element e, Stack *st, Dict *dict, int depth) {
    ind(depth);
    printf("評価: ");
    brief(&e);
    printf("\n");

    if (e.type == ELEM_EXEC_NAME) {
        Element found;

        if (dict_get(dict, e.u.name, &found)) {
            ind(depth);
            printf("  辞書を引く → ");
            brief(&found);
            printf("\n");
            switch (found.type) {
            case ELEM_PRIMITIVE:
                // 制御構造だけは中身も追う。それ以外は C関数を呼ぶだけ。
                if (strcmp(e.u.name, "jmp") == 0 || strcmp(e.u.name, "jmp_not_if") == 0) {
                    // jmp 系は「実行中のフレームの pc」を書き換える命令。
                    // このモードにはフレームが無いので実行できない。
                    ind(depth);
                    printf("  %s は継続スタックのフレームが必要です。\n", e.u.name);
                    ind(depth);
                    printf("  ./trace -vm \"...\" で実行してください\n");
                    exit(1);
                }
                if (!trace_control(e.u.name, st, dict, depth)) {
                    ContStack *cs = contstack_new();

                    ind(depth);
                    printf("  C関数なので呼ぶ\n");
                    found.u.fn(st, cs, dict);
                    // プリミティブが実行予定を積んだ場合も、完了させてから戻る。
                    run(cs, st, dict);
                    contstack_free(cs);
                }
                break;
            case ELEM_EXEC_ARRAY:
                ind(depth);
                printf("  実行可能配列なので中身を順に実行する\n");
                trace_exec_array(found.u.exec_array, st, dict, depth + 1);
                break;
            default:
                ind(depth);
                printf("  値なので積む\n");
                stack_push(st, found);
                break;
            }
        } else {
            ind(depth);
            printf("  辞書に無い → unknown word: %s\n", e.u.name);
        }
    } else {
        ind(depth);
        printf("  値なので積む\n");
        stack_push(st, e);
    }
    dump_stack(st, depth + 1);
}

// 配列の中に jmp / jmp_not_if が含まれているか
// （12章で ifelse をコンパイル時に展開するようになったので、関数の本体に現れる）
static bool contains_jmp(ExecArray *ea) {
    for (int i = 0; i < ea->count; i++) {
        Element *e = &ea->items[i];
        if (e->type == ELEM_EXEC_NAME
            && (strcmp(e->u.name, "jmp") == 0 || strcmp(e->u.name, "jmp_not_if") == 0)) {
            return true;
        }
    }
    return false;
}

// eval_exec_array と同じことを、途中経過を出しながらやる
static void trace_exec_array(ExecArray *ea, Stack *st, Dict *dict, int depth) {
    // jmp は pc を書き換える命令なので、1要素ずつ辿るこのモードでは追えない。
    // 中身はまとめて本物の実行エンジンで動かし、結果だけ表示する。
    if (contains_jmp(ea)) {
        ind(depth);
        printf("（jmp を含むので中身はまとめて実行する。1ステップずつ見るには -vm）\n");
        eval_exec_array(ea, st, dict);
        return;
    }
    for (int i = 0; i < ea->count; i++) {
        ind(depth);
        printf("items[%d]:\n", i);
        trace_element(ea->items[i], st, dict, depth + 1);
    }
}

// eval と同じことを、途中経過を出しながらやる
static void trace_eval(const char *code, Stack *st, Dict *dict) {
    CharSource src = src_from(code);
    Token t;
    int n = 0;

    printf("入力: %s\n\n", code);
    while (parse_one(&src, &t)) {
        n++;
        switch (t.type) {
        case TOKEN_INT:
            printf("[%d] トークン: %d  (%s)\n", n, t.u.ival, token_type_name(t.type));
            printf("    値なので積む\n");
            stack_push(st, element_int(t.u.ival));
            break;

        case TOKEN_LITERAL_NAME:
            printf("[%d] トークン: /%s  (%s)\n", n, t.u.name, token_type_name(t.type));
            printf("    名前そのものを積む（実行しない）\n");
            stack_push(st, element_literal_name(t.u.name));
            break;

        case TOKEN_OPEN_BRACE: {
            ExecArray *ea;
            Element e;

            printf("[%d] トークン: {  (%s)\n", n, token_type_name(t.type));
            printf("    compile_exec_array で } まで読み取る（実行はしない）\n");
            ea = compile_exec_array(&src);
            e = element_exec_array(ea);
            printf("    できたもの: ");
            detail(&e, 1);
            printf("    それを1個の値として積む\n");
            stack_push(st, e);
            break;
        }

        case TOKEN_EXEC_NAME:
            // def も含めて特別扱いは無い。すべて辞書を引いて振り分ける。
            printf("[%d] トークン: %s  (%s)\n", n, t.u.name, token_type_name(t.type));
            trace_element(element_exec_name(t.u.name), st, dict, 1);
            printf("\n");
            continue;

        default:
            printf("[%d] トークン: (%s) — 無視\n", n, token_type_name(t.type));
            break;
        }
        dump_stack(st, 2);
        printf("\n");
    }
    fclose(src.fp);
}

static void run_trace(const char *code) {
    Stack *st = stack_new(100);
    Dict *dict = dict_new();

    register_primitives(dict);

    printf("=========================================\n");
    trace_eval(code, st, dict);
    printf("最終的なスタック:\n");
    dump_stack(st, 1);
    printf("\n");

    stack_free(st);
    dict_free(dict);
}

// ===================================================================
// -vm モード: 11章の run と同じループを、継続スタックの全フレームを
// 表示しながら1ステップずつ回す。frame と pc の動きを見るためのもの。
// ===================================================================

#define VM_LABEL_MAX 256

// 各フレームに「誰が積んだか」の名前を付けて表示する
static char vm_labels[VM_LABEL_MAX][ELEMENT_NAME_MAX_SIZE + 16];

static void vm_show_frames(ContStack *cs) {
    int n = contstack_size(cs);

    printf("    継続: ");
    if (n == 0) {
        printf("[空]");
    }
    for (int i = 0; i < n; i++) {
        Continuation *f = contstack_at(cs, i);
        printf("[%s pc=%d/%d]", vm_labels[i], f->pc, f->ea->count);
    }
    printf("\n");
}

// 比較演算子なら C の記号を返す。比較でなければ NULL。
// 結果はスタックに 1 / 0 が残るだけなので、何を比べたかを別に表示するため。
static const char *compare_symbol(const Element *e) {
    static const struct { const char *name; const char *sym; } ops[] = {
        { "eq", "==" }, { "ne", "!=" },
        { "lt", "<" },  { "gt", ">" },
        { "le", "<=" }, { "ge", ">=" },
    };

    if (e->type != ELEM_EXEC_NAME) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++) {
        if (strcmp(e->u.name, ops[i].name) == 0) {
            return ops[i].sym;
        }
    }
    return NULL;
}

static void vm_run_trace(const char *code) {
    Stack *st = stack_new(100);
    Dict *dict = dict_new();
    ContStack *cs = contstack_new();
    char *wrapped;
    CharSource src;
    ExecArray *program;
    int step = 0;

    register_primitives(dict);

    // プログラム全体を { ... } の中身としてコンパイルし、1枚目のフレームにする
    wrapped = malloc(strlen(code) + 3);
    sprintf(wrapped, "%s }", code);
    src = src_from(wrapped);
    program = compile_exec_array(&src);
    fclose(src.fp);

    printf("=========================================\n");
    printf("入力: %s\n\n", code);

    // コンパイル結果（main の items）をすべて表示する。
    // 実行時に飛ばされる要素も含めて、配列に何が入っているかが分かる。
    printf("コンパイル結果（main の items）:\n");
    for (int k = 0; k < program->count; k++) {
        printf("    items[%d] = ", k);
        detail(&program->items[k], 2);
        if (program->items[k].type != ELEM_EXEC_ARRAY) {
            printf("\n");
        }
    }
    printf("\n");

    contstack_push(cs, program);
    snprintf(vm_labels[0], sizeof vm_labels[0], "main");
    printf("開始: プログラム全体を1枚目のフレームとして積む\n");
    vm_show_frames(cs);
    printf("\n");

    // ---- ここから run と同じループ ----
    while (!contstack_is_empty(cs)) {
        Continuation *frame = contstack_top(cs);
        int depth = contstack_size(cs);
        Element e;
        int before;
        const char *cmp;
        Element lhs, rhs;

        step++;

        // ① 実行し終わっていたら捨てる（呼び出し元に戻る）
        if (frame->pc >= frame->ea->count) {
            printf("(%d) %s は pc=%d で最後まで実行した → フレームを捨てて戻る\n",
                   step, vm_labels[depth - 1], frame->pc);
            contstack_pop(cs);
            vm_show_frames(cs);
            printf("\n");
            continue;
        }

        // ② 今の位置の要素を取り出して、pc を進める
        e = frame->ea->items[frame->pc];
        printf("(%d) %s の items[%d] = ", step, vm_labels[depth - 1], frame->pc);
        brief(&e);
        printf("  （pc %d → %d）\n", frame->pc, frame->pc + 1);
        frame->pc++;

        // 比較演算子は pop される前に、比べる2つの値を覚えておく
        cmp = compare_symbol(&e);
        if (cmp != NULL && stack_size(st) >= 2) {
            rhs = stack_index(st, 0);
            lhs = stack_index(st, 1);
        } else {
            cmp = NULL;
        }

        // ③ 評価する
        before = contstack_size(cs);
        eval_element_vm(e, st, cs, dict);

        if (cmp != NULL) {
            int result = stack_index(st, 0).u.ival;

            printf("    %d %s %d → %d（%s）\n",
                   lhs.u.ival, cmp, rhs.u.ival, result, result ? "真" : "偽");
        }

        // 評価の結果フレームが積まれたら、その名前を記録する
        if (contstack_size(cs) > before) {
            const char *who = (e.type == ELEM_EXEC_NAME) ? e.u.name : "{ }";
            snprintf(vm_labels[contstack_size(cs) - 1],
                     sizeof vm_labels[0], "%s", who);
            printf("    → 新しいフレームを積んだ（呼び出し）\n");
        }
        vm_show_frames(cs);
        dump_stack(st, 2);
        printf("\n");
    }

    printf("最終的なスタック:\n");
    dump_stack(st, 1);

    free(wrapped);
    stack_free(st);
    dict_free(dict);
    contstack_free(cs);
}

int main(int argc, char **argv) {
    // ./trace -vm "コード"  → 継続スタックの動きを表示
    if (argc >= 3 && strcmp(argv[1], "-vm") == 0) {
        vm_run_trace(argv[2]);
        return 0;
    }
    if (argc >= 2) {
        run_trace(argv[1]);
        return 0;
    }

    // 引数なしのときは、代表的な3例をトレースする
    run_trace("/double { 2 mul } def 5 double");
    run_trace("/inner { 2 mul } def /outer { 5 inner } def outer");
    run_trace("{ 1 { 2 3 add } }");
    return 0;
}
