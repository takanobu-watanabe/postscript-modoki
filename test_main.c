#include <stdio.h>
#include <string.h>
#include "parser.h"
#include "test_util.h"
#include "stack.h"
#include "eval.h"
#include "dict.h"
#include "primitives.h"
#include "exec_array.h"
#include "cont.h"

// Element が ELEM_INT で、期待した値を持つことを検証する
#define UT_EQ_ELEM_INT(expected, actual) do { \
    Element _el = (actual); \
    UT_EQ_INT(ELEM_INT, _el.type); \
    if (_el.type == ELEM_INT) { \
        UT_EQ_INT((expected), _el.u.ival); \
    } \
} while (0)

// 文字列を入力にした CharSource を作るヘルパー
CharSource make_src_from_string(const char *str) {
    CharSource src;

    src.fp = fmemopen((void*)str, strlen(str), "r");
    return src;
}

// 演習1-1: "123" を渡して 123 が読めることを確認する
void test_parse_int_single(void) {
    int num;
    bool b;

    CharSource src = make_src_from_string("123");
    b = parse_int(&src, &num);
    UT_TRUE(b);
    if (b){
        UT_EQ_INT(123, num);
    }
    fclose(src.fp);
}

// 演習1-1: 前後に空白がある "  42  " でも読めることを確認する
void test_parse_int_with_spaces(void) {
    int num;
    bool b;

    CharSource src = make_src_from_string("  42  ");
    b = parse_int(&src, &num);
    UT_TRUE(b);
    if (b){
        UT_EQ_INT(42, num);
    }
    fclose(src.fp);
}

// 演習1-2: "1 2 3" から3回呼んで 1, 2, 3 を順に取り出せることを確認する
void test_parse_int_multiple(void) {
    CharSource src = make_src_from_string("1 2 3");
    int num;
    bool b;
  
    b = parse_int(&src, &num);
    UT_TRUE(b);
    if (b){
        UT_EQ_INT(1, num);
    }
    b = parse_int(&src, &num);
    UT_TRUE(b);
    if (b){
        UT_EQ_INT(2, num);
    }
    b = parse_int(&src, &num);
    UT_TRUE(b);
    if (b){
        UT_EQ_INT(3, num);
    }
    fclose(src.fp);
}

// 演習1-2: 空文字列 "" では false が返ることを確認する
void test_parse_int_empty(void) {
    CharSource src = { .fp = tmpfile() };
    UT_TRUE(src.fp != NULL);
    if (src.fp == NULL) {
        return;
    }

    int num = 0;
    bool b = parse_int(&src, &num);
    UT_TRUE(!b);

    fclose(src.fp);
}

// 演習1-3（発展）: "abc" では false が返り、'a' が ungetc で正しく戻っていることを確認する
void test_parse_int_non_digit(void) { 
    CharSource src = make_src_from_string("abc");
    int num;
    bool b;
  
    b = parse_int(&src, &num);
    UT_TRUE(!b);
    UT_EQ_INT('a', cl_getc(&src));    // 戻された 'a' を読める
    fclose(src.fp);
}

// int の最大値までは読み取れることを確認する
void test_parse_int_max(void) {
    char input[sizeof(int) * CHAR_BIT + 2];
    snprintf(input, sizeof(input), "%d", INT_MAX);
    CharSource src = make_src_from_string(input);
    UT_TRUE(src.fp != NULL);
    if (src.fp == NULL) {
        return;
    }

    int num = 0;
    bool b = parse_int(&src, &num);
    UT_TRUE(b);
    if (b) {
        UT_EQ_INT(INT_MAX, num);
    }
    fclose(src.fp);
}

// int の最大値を1超える入力では false が返ることを確認する
void test_parse_int_overflow(void) {
    char input[sizeof(int) * CHAR_BIT + 2];
    // テスト用の数値を作る計算自体がオーバーフローしないよう unsigned を使う
    snprintf(input, sizeof(input), "%u", (unsigned int)INT_MAX + 1U);
    CharSource src = make_src_from_string(input);
    UT_TRUE(src.fp != NULL);
    if (src.fp == NULL) {
        return;
    }

    int num = 0;
    bool b = parse_int(&src, &num);
    UT_TRUE(!b);
    fclose(src.fp);
}

void test_parse_one_int(void){
    CharSource src = make_src_from_string("42");
    Token t = { .type = TOKEN_EOF };

    bool b = parse_one(&src, &t);
    UT_TRUE(b);
    if (b){
        UT_EQ_INT(TOKEN_INT, t.type);
        if (t.type == TOKEN_INT) {
            UT_EQ_INT(42, t.u.ival);
        }
    }
    fclose(src.fp);
}

void test_parse_one_exec_name(void) {
    CharSource src = make_src_from_string("add");
    Token t = { .type = TOKEN_EOF };

    bool b = parse_one(&src, &t);
    UT_TRUE(b);
    if (b){
        UT_EQ_INT(TOKEN_EXEC_NAME, t.type);
        if (t.type == TOKEN_EXEC_NAME) {
            UT_EQ_INT(0, strcmp("add", t.u.name));
        }
    }
    fclose(src.fp);
}

void test_parse_one_literal_name(void) {
    CharSource src = make_src_from_string("/double");
    Token t = { .type = TOKEN_EOF };

    bool b = parse_one(&src, &t);
    UT_TRUE(b);
    if (b){
        UT_EQ_INT(TOKEN_LITERAL_NAME, t.type);
        if (t.type == TOKEN_LITERAL_NAME) {
            UT_EQ_INT(0, strcmp("double", t.u.name));
        }
    }
    fclose(src.fp);
}

void test_parse_one_open_brace(void) {
    CharSource src = make_src_from_string("{");
    Token t = { .type = TOKEN_EOF };

    bool b = parse_one(&src, &t);
    UT_TRUE(b);
    if (b){
        UT_EQ_INT(TOKEN_OPEN_BRACE, t.type);
    }
    fclose(src.fp);
}

void test_parse_one_close_brace(void) {
    CharSource src = make_src_from_string("}");
    Token t = { .type = TOKEN_EOF };

    bool b = parse_one(&src, &t);
    UT_TRUE(b);
    if (b){
        UT_EQ_INT(TOKEN_CLOSE_BRACE, t.type);
    }
    fclose(src.fp);
}

void test_stack_push_pop(void) {
    Stack *st = stack_new(10);
    stack_push(st, element_int(1));
    stack_push(st, element_int(2));
    
    UT_EQ_ELEM_INT(2, stack_pop(st));

    UT_EQ_ELEM_INT(1, stack_pop(st));

    UT_TRUE(stack_is_empty(st));

    
    stack_free(st);
}

void test_stack_top_does_not_pop(void) {
    Stack *st = stack_new(10);

    stack_push(st, element_int(5));
    UT_EQ_ELEM_INT(5, stack_top(st));
    UT_EQ_INT(1, stack_size(st));
    stack_free(st);
}

void test_stack_exch(void) {
    Stack *st = stack_new(10);

    stack_push(st, element_int(1));
    stack_push(st, element_int(2));
    stack_exch(st);

    UT_EQ_INT(2, stack_size(st)); 
    UT_EQ_ELEM_INT(1, stack_pop(st));
    UT_EQ_ELEM_INT(2, stack_pop(st));
    stack_free(st);

}


void test_eval_push_only(void) {
    CharSource src = make_src_from_string("1 2 3");
    Dict *dict = dict_new();
    Stack *st = stack_new(10);
    eval(&src, st, dict);
    // size が 3、pop が 3 → 2 → 1 の順
    UT_EQ_INT(3, stack_size(st)); 
    UT_EQ_ELEM_INT(3, stack_pop(st));
    UT_EQ_ELEM_INT(2, stack_pop(st));
    UT_EQ_ELEM_INT(1, stack_pop(st));
    stack_free(st);
}

void test_eval_add(void) {
    CharSource src = make_src_from_string("1 2 add");
    Stack *st = stack_new(10);
    Dict *dict = dict_new();

    register_primitives(dict);
    eval(&src, st, dict);
    UT_EQ_INT(1, stack_size(st)); 
    UT_EQ_ELEM_INT(3, stack_pop(st));
    stack_free(st);
    dict_free(dict);
}

void test_eval_dup(void) {
    CharSource src = make_src_from_string("5 dup add");
    Stack *st = stack_new(10);
    Dict *dict = dict_new();
    
    register_primitives(dict);
    eval(&src, st, dict);
    UT_EQ_INT(1, stack_size(st)); 
    UT_EQ_ELEM_INT(10, stack_pop(st));
    stack_free(st);
    dict_free(dict);
}

void test_eval_pop(void) {
    CharSource src = make_src_from_string("1 2 pop");
    Stack *st = stack_new(10);
    Dict *dict = dict_new();
    
    register_primitives(dict);
    eval(&src, st, dict);
    UT_EQ_INT(1, stack_size(st)); 
    UT_EQ_ELEM_INT(1, stack_pop(st));
    stack_free(st);
    dict_free(dict);
}

void test_eval_sub(void) {
    CharSource src = make_src_from_string("10 3 sub");
    Stack *st = stack_new(10);
    Dict *dict = dict_new();
    register_primitives(dict);

    eval(&src, st, dict);
    UT_EQ_ELEM_INT(7, stack_pop(st));

    stack_free(st);
    dict_free(dict);
}

void test_eval_mul(void) {
    CharSource src = make_src_from_string("10 3 mul");
    Stack *st = stack_new(10);
    Dict *dict = dict_new();
    register_primitives(dict);

    eval(&src, st, dict);
    UT_EQ_ELEM_INT(30, stack_pop(st));
    stack_free(st);
    dict_free(dict);
}

void test_eval_div(void) {
    CharSource src = make_src_from_string("10 2 div");
    Stack *st = stack_new(10);
    Dict *dict = dict_new();
    register_primitives(dict);

    eval(&src, st, dict);
    UT_EQ_ELEM_INT(5, stack_pop(st));
    stack_free(st);
    dict_free(dict);
}

void test_eval_sub_reversed(void) {
    CharSource src = make_src_from_string("3 10 sub");
    Stack *st = stack_new(10);
    Dict *dict = dict_new();
    register_primitives(dict);

    eval(&src, st, dict);
    UT_EQ_ELEM_INT(-7, stack_pop(st));
    stack_free(st);
    dict_free(dict);
}


// コードを評価して、スタックのトップが期待値かを確認する。
// 比較演算子のように「入力を変えて同じ形を繰り返す」テスト用。
// 失敗したときはどの入力で落ちたかも表示する。
static void check_eval_int(const char *code, int expected) {
    int fail_before = g_test_fail_count;
    CharSource src = make_src_from_string(code);
    Stack *st = stack_new(10);
    Dict *dict = dict_new();

    register_primitives(dict);
    eval(&src, st, dict);
    UT_EQ_ELEM_INT(expected, stack_pop(st));

    if (g_test_fail_count != fail_before) {
        printf("    ↑ 入力: \"%s\"\n", code);
    }

    stack_free(st);
    dict_free(dict);
    fclose(src.fp);
}

void test_eval_eq(void) {
    check_eval_int("3 3 eq", 1);
    check_eval_int("3 4 eq", 0);
}

void test_eval_ne(void) {
    check_eval_int("3 4 ne", 1);
    check_eval_int("3 3 ne", 0);
}

// lt / gt は境界（等しいとき偽）まで見ると le / ge との違いが固定できる
void test_eval_lt(void) {
    check_eval_int("1 2 lt", 1);
    check_eval_int("2 1 lt", 0);   // 引数の順序を取り違えていないか
    check_eval_int("2 2 lt", 0);   // 等しいときは偽
}

void test_eval_gt(void) {
    check_eval_int("2 1 gt", 1);
    check_eval_int("1 2 gt", 0);
    check_eval_int("2 2 gt", 0);
}

void test_eval_le(void) {
    check_eval_int("1 2 le", 1);
    check_eval_int("2 2 le", 1);   // 等しいときは真。ここが lt との違い
    check_eval_int("3 2 le", 0);
}

void test_eval_ge(void) {
    check_eval_int("2 1 ge", 1);
    check_eval_int("2 2 ge", 1);   // 等しいときは真。ここが gt との違い
    check_eval_int("1 2 ge", 0);
}

void test_eval_mod(void) {
    check_eval_int("10 3 mod", 1);
    check_eval_int("10 5 mod", 0);
    check_eval_int("3 10 mod", 3);   // 割られる数の方が小さいとき
}

void test_eval_add_twice(void) {
    CharSource src = make_src_from_string("1 2 add 10 add");
    Stack *st = stack_new(10);
    Dict *dict = dict_new();

    register_primitives(dict);
    eval(&src, st, dict);
    UT_EQ_INT(1, stack_size(st)); 
    UT_EQ_ELEM_INT(13, stack_pop(st));
    stack_free(st);
}

void test_dict_put_get(void) {
    Dict *dict = dict_new();
    Element val;
    bool found;

    dict_put(dict, "x", element_int(5));

    found = dict_get(dict, "x", &val);
    UT_TRUE(found);
    if (found) {
        UT_EQ_ELEM_INT(5, val);
    }

    dict_free(dict);
}

void test_dict_put_overwrites(void) {
    Dict *dict = dict_new();
    Element val;
    bool found;

    dict_put(dict, "x", element_int(5));
    dict_put(dict, "x", element_int(10));

    found = dict_get(dict, "x", &val);
    UT_TRUE(found);
    if (found) {
        UT_EQ_ELEM_INT(10, val);
    }

    dict_free(dict);
}

void test_dict_get_undefined_returns_false(void) {
    Dict *dict = dict_new();
    Element val;
    bool found;

    found = dict_get(dict, "y", &val);
    UT_TRUE(!found);

    dict_free(dict);
}

void test_eval_def_and_use(void) {
    CharSource src = make_src_from_string("/x 5 def x x add");
    Stack *st = stack_new(10);
    Dict *dict = dict_new();

    register_primitives(dict);
    eval(&src, st, dict);
    UT_EQ_INT(1, stack_size(st)); 
    UT_EQ_ELEM_INT(10, stack_pop(st));

    stack_free(st);
    dict_free(dict);
}

void test_dict_many_entries(void) {
    Dict *dict = dict_new();

    const char *values[] = {"add", "sub", "mul", "div", "x", "y", "z", "foo", "bar", "baz", "hello", "world"};
    int count = sizeof(values) / sizeof(values[0]);

    for (int i = 0; i < count; i++) {
        dict_put(dict, values[i], element_int(i));
    }
    for (int i = 0; i < count; i++) {
        Element out_value;

        bool found = dict_get(dict, values[i], &out_value);
        UT_TRUE(found);
        if (found) {
            UT_EQ_ELEM_INT(i, out_value);
        }
    }
    dict_free(dict);
}

void test_exec_array_new(void) {
    ExecArray *ea = exec_array_new(3);

    UT_EQ_INT(3, ea->count);

    ea->items[0] = element_int(1);
    ea->items[1] = element_int(2);
    ea->items[2] = element_int(3);

    UT_EQ_ELEM_INT(1, ea->items[0]);
    UT_EQ_ELEM_INT(2, ea->items[1]);
    UT_EQ_ELEM_INT(3, ea->items[2]);

    exec_array_free(ea);
}

void test_compile_simple(void) {
    // { は呼び出し元（eval）が読み終わっている前提なので、入力に { は含めない
    CharSource src = make_src_from_string("1 2 add }");

    ExecArray *ea = compile_exec_array(&src);

    if (!ea) {
        return;
    }

    UT_EQ_INT(3, ea->count);

    UT_EQ_ELEM_INT(1, ea->items[0]);
    UT_EQ_ELEM_INT(2, ea->items[1]);

    UT_EQ_INT(ELEM_EXEC_NAME, ea->items[2].type);
    if (ea->items[2].type == ELEM_EXEC_NAME) {
        UT_EQ_INT(0, strcmp("add", ea->items[2].u.name));
    }

    exec_array_free(ea);
    fclose(src.fp);
}

void test_eval_exec_array_def_and_call(void) {
    CharSource src = make_src_from_string("/double { 2 mul } def 5 double");
    Stack *st = stack_new(10);
    Dict *dict = dict_new();
    register_primitives(dict);

    eval(&src, st, dict);

    UT_EQ_ELEM_INT(10, stack_pop(st));

    stack_free(st);
    dict_free(dict);
    fclose(src.fp);
}

void test_compile_nested(void) {
    CharSource src = make_src_from_string("1 { 2 3 add } }");
    ExecArray *ea = compile_exec_array(&src);

    UT_EQ_INT(2, ea->count);
    UT_EQ_ELEM_INT(1, ea->items[0]);

    UT_EQ_INT(ELEM_EXEC_ARRAY, ea->items[1].type);
    if (ea->items[1].type == ELEM_EXEC_ARRAY) {
        ExecArray *inner = ea->items[1].u.exec_array;
        UT_EQ_INT(3, inner->count);

        UT_EQ_ELEM_INT(2, inner->items[0]);
        UT_EQ_ELEM_INT(3, inner->items[1]);

        // 内側でも add は辞書引きされず、名前のまま保持されている
        UT_EQ_INT(ELEM_EXEC_NAME, inner->items[2].type);
        if (inner->items[2].type == ELEM_EXEC_NAME) {
            UT_EQ_INT(0, strcmp("add", inner->items[2].u.name));
        }
    }

    exec_array_free(ea);
    fclose(src.fp);
}

void test_compile_grows_beyond_capacity(void) {
    char code[256];
    int n = 20;
    int pos = 0;

    // "1 2 3 ... 20 }" という文字列を組み立てる
    for (int i = 1; i <= n; i++) {
        pos += snprintf(code + pos, sizeof(code) - pos, "%d ", i);
    }
    snprintf(code + pos, sizeof(code) - pos, "}");

    CharSource src = make_src_from_string(code);
    ExecArray *ea = compile_exec_array(&src);

    UT_EQ_INT(n, ea->count);

    for (int i = 0; i < n; i++) {
        UT_EQ_ELEM_INT(i + 1, ea->items[i]);
    }

    exec_array_free(ea);
    fclose(src.fp);
}

void test_eval_exch(void) {
    CharSource src = make_src_from_string("1 2 exch");
    Stack *st = stack_new(10);
    Dict *dict = dict_new();
    register_primitives(dict);

    eval(&src, st, dict);

    // pop すると 1 → 2 の順（入れ替わっているので、積んだ順と同じ順で出る）
    UT_EQ_ELEM_INT(1, stack_pop(st));
    UT_EQ_ELEM_INT(2, stack_pop(st));

    stack_free(st);
    dict_free(dict);
    fclose(src.fp);
}


// --- if / ifelse -------------------------------------------------------

void test_eval_if_true(void) {
    check_eval_int("1 { 42 } if", 42);
}

// 偽のときは何も積まれないので、トップを見る check_eval_int は使えない。
// 下に積んでおいた 99 がそのまま残っていることで「何もしなかった」を確認する。
void test_eval_if_false(void) {
    CharSource src = make_src_from_string("99 0 { 42 } if");
    Stack *st = stack_new(10);
    Dict *dict = dict_new();

    register_primitives(dict);
    eval(&src, st, dict);

    UT_EQ_INT(1, stack_size(st));      // if は bool と proc の2つだけを消費する
    UT_EQ_ELEM_INT(99, stack_pop(st));

    stack_free(st);
    dict_free(dict);
    fclose(src.fp);
}

// 実行されるブロックは、その下に積まれている値を使える
// （{ 2 mul } の中に 5 は書かれていないが、呼ぶ前に積んだ 5 が掛けられる）
void test_eval_if_uses_stack_below(void) {
    check_eval_int("5 1 { 2 mul } if", 10);
}

// ifelse は真偽の両方を確認しないと、条件を逆に実装していても気づけない
void test_eval_ifelse_true(void) {
    check_eval_int("1 { 10 } { 20 } ifelse", 10);
    check_eval_int("2 { 10 } { 20 } ifelse", 10);
    check_eval_int("-1 { 10 } { 20 } ifelse", 10);
}

void test_eval_ifelse_false(void) {
    check_eval_int("0 { 10 } { 20 } ifelse", 20);
}

// 全体を実行可能配列にして、呼び出し元のフレームを作る。
// 分岐のフレームを実行し終えたら、元の配列の 3 add に戻ることを確認する。
void test_eval_ifelse_resumes_caller(void) {
    check_eval_int("/choose { 1 { 10 } { 20 } ifelse 3 add } def choose", 13);
    check_eval_int("/choose { 0 { 10 } { 20 } ifelse 3 add } def choose", 23);
}

// パーサー単体：-5 が TOKEN_INT として読めるか
void test_parse_one_negative_int(void) {
    CharSource src = make_src_from_string("-5");
    Token t = { .type = TOKEN_EOF };

    bool b = parse_one(&src, &t);
    UT_TRUE(b);
    if (b) {
        UT_EQ_INT(TOKEN_INT, t.type);
        if (t.type == TOKEN_INT) {
            UT_EQ_INT(-5, t.u.ival);
        }
    }
    fclose(src.fp);
}

// eval 層：負のリテラルがそのまま値として扱えるか
void test_eval_negative_literal(void) {
    check_eval_int("-5", -5);
    check_eval_int("-3 -4 add", -7);
    check_eval_int("10 -3 sub", 13);
}

// n index : てっぺんから n 個下をコピーして積む
void test_eval_index(void) {
    check_eval_int("1 2 3 0 index", 3);   // 0 個下＝てっぺん。dup と同じ
    check_eval_int("1 2 3 1 index", 2);
    check_eval_int("1 2 3 2 index", 1);
}

// index は取り除かずにコピーするだけなので、元の3つが残って4つになる
void test_eval_index_does_not_consume(void) {
    CharSource src = make_src_from_string("1 2 3 2 index");
    Stack *st = stack_new(10);
    Dict *dict = dict_new();

    register_primitives(dict);
    eval(&src, st, dict);

    UT_EQ_INT(4, stack_size(st));
    UT_EQ_ELEM_INT(1, stack_pop(st));   // コピーされた 1
    UT_EQ_ELEM_INT(3, stack_pop(st));
    UT_EQ_ELEM_INT(2, stack_pop(st));
    UT_EQ_ELEM_INT(1, stack_pop(st));

    stack_free(st);
    dict_free(dict);
    fclose(src.fp);
}

// { 条件 } { 本体 } while
void test_eval_while(void) {
    check_eval_int("5 { dup 0 gt } { 1 sub } while", 0);   // 5 から 0 まで減らす
    check_eval_int("0 { dup 0 gt } { 1 sub } while", 0);   // 最初から偽なら本体は動かない
}

// 値 番号 store_local / 番号 load_local
// locals はフレームの中にあるので、関数（{ }）の中で使う。
void test_eval_local(void) {
    // 入れた値を何度でも読める
    check_eval_int("/f { 5 0 store_local 0 load_local 0 load_local add } def f", 10);
    // 番号ごとに別の箱
    check_eval_int("/g { 3 0 store_local 4 1 store_local 0 load_local 1 load_local mul } def g", 12);
    // フレームごとに別の箱：inner が locals[0] を書き換えても outer の locals[0] は 7 のまま
    check_eval_int("/inner { 99 0 store_local } def"
                   " /outer { 7 0 store_local inner 0 load_local } def outer", 7);
}

// % から行末までがコメントとして読み飛ばされる
void test_parse_comment(void) {
    check_eval_int("1 2 add % これはコメント\n", 3);
    check_eval_int("% 先頭がコメント\n1 2 add", 3);
    check_eval_int("1 % コメント\n2 % コメント\nadd", 3);
}

// 演習9-4: 絶対値。if が真・偽の両方で正しく働くことを実際のプログラムで確認する
void test_eval_abs(void) {
    check_eval_int("-5 dup 0 lt { -1 mul } if", 5);   // 負 → 符号反転する
    check_eval_int("5 dup 0 lt { -1 mul } if", 5);    // 正 → 何もしない
    check_eval_int("0 dup 0 lt { -1 mul } if", 0);    // 0 は負ではないので何もしない
}

void test_eval_print_consumes(void) {
    CharSource src = make_src_from_string("99 42 print");
    Stack *st = stack_new(10);
    Dict *dict = dict_new();

    register_primitives(dict);
    eval(&src, st, dict);

    UT_EQ_INT(1, stack_size(st));      // 42 だけ消えた
    UT_EQ_ELEM_INT(99, stack_pop(st));

    stack_free(st);
    dict_free(dict);
    fclose(src.fp);
}

void test_contstack_push_pop(void) {
    ContStack *cs = contstack_new();
    CharSource src = make_src_from_string("1 2 add }");
    ExecArray *ea = compile_exec_array(&src);

    UT_TRUE(contstack_is_empty(cs));
    UT_EQ_INT(0, contstack_size(cs));

    contstack_push(cs, ea);
    UT_EQ_INT(1, contstack_size(cs));
    UT_TRUE(!contstack_is_empty(cs));

    // 積んだ直後は pc が 0
    UT_EQ_INT(0, contstack_top(cs)->pc);

    // pc を進めると、次に top を取ったときも進んだ値が見える
    contstack_top(cs)->pc = 2;
    UT_EQ_INT(2, contstack_top(cs)->pc);

    contstack_pop(cs);
    UT_TRUE(contstack_is_empty(cs));

    exec_array_free(ea);
    contstack_free(cs);
    fclose(src.fp);
}

void test_run_add(void) {
    CharSource src = make_src_from_string("1 2 add }");
    ExecArray *ea = compile_exec_array(&src);
    Stack *st = stack_new(10);
    Dict *dict = dict_new();
    ContStack *cs = contstack_new();

    register_primitives(dict);
    contstack_push(cs, ea);        // 「これから ea を実行する」
    run(cs, st, dict);             // 継続スタックが空になるまで回す

    UT_EQ_INT(1, stack_size(st));
    UT_EQ_ELEM_INT(3, stack_pop(st));
    UT_TRUE(contstack_is_empty(cs));   // 最後は空になっているはず

    stack_free(st);
    dict_free(dict);
    contstack_free(cs);
    exec_array_free(ea);
    fclose(src.fp);
}

void test_run_nested(void) {
    CharSource src1 = make_src_from_string("2 mul }");
    ExecArray *inner = compile_exec_array(&src1);

    CharSource src2 = make_src_from_string("5 inner }");
    ExecArray *outer = compile_exec_array(&src2);

    Stack *st = stack_new(10);
    Dict *dict = dict_new();
    ContStack *cs = contstack_new();

    register_primitives(dict);
    dict_put(dict, "inner", element_exec_array(inner));   // 先に辞書に登録

    contstack_push(cs, outer);
    run(cs, st, dict);

    UT_EQ_ELEM_INT(10, stack_pop(st));
    UT_TRUE(contstack_is_empty(cs));

    stack_free(st);
    dict_free(dict);
    contstack_free(cs);
    exec_array_free(outer);
    exec_array_free(inner);
    fclose(src1.fp);
    fclose(src2.fp);
}

void test_run_jmp(void) {
    CharSource src = make_src_from_string("1 2 jmp 100 200 3 }");
    ExecArray *ea = compile_exec_array(&src);
    Stack *st = stack_new(10);
    Dict *dict = dict_new();
    ContStack *cs = contstack_new();

    register_primitives(dict);
    contstack_push(cs, ea);
    run(cs, st, dict);

    UT_EQ_INT(2, stack_size(st));        // 100 と 200 は積まれていない
    UT_EQ_ELEM_INT(3, stack_pop(st));
    UT_EQ_ELEM_INT(1, stack_pop(st));

    stack_free(st);
    dict_free(dict);
    contstack_free(cs);
    exec_array_free(ea);
    fclose(src.fp);
}


// 条件 n jmp_not_if を { } の中で run して、残ったスタックを配列で検証する
static void check_run_stack(const char *code, const int *expected, int count) {
    int fail_before = g_test_fail_count;
    CharSource src = make_src_from_string(code);
    ExecArray *ea = compile_exec_array(&src);
    Stack *st = stack_new(10);
    Dict *dict = dict_new();
    ContStack *cs = contstack_new();

    register_primitives(dict);
    contstack_push(cs, ea);
    run(cs, st, dict);

    UT_EQ_INT(count, stack_size(st));
    if (stack_size(st) == count) {
        for (int i = count - 1; i >= 0; i--) {   // てっぺんから順に取り出す
            UT_EQ_ELEM_INT(expected[i], stack_pop(st));
        }
    }
    if (g_test_fail_count != fail_before) {
        printf("    ↑ 入力: \"%s\"\n", code);
    }

    stack_free(st);
    dict_free(dict);
    contstack_free(cs);
    exec_array_free(ea);
    fclose(src.fp);
}

void test_run_jmp_not_if(void) {
    // 真なら飛ばない
    int t[] = { 100, 200, 3 };
    check_run_stack("1 2 jmp_not_if 100 200 3 }", t, 3);

    // 偽なら 100 200 を飛ばす
    int f[] = { 3 };
    check_run_stack("0 2 jmp_not_if 100 200 3 }", f, 1);
}

// --- 12章: ifelse の静的コード生成 -----------------------------------------

// { } の中の cond { A } { B } ifelse が jmp の列に展開されていること
//   1 { 10 } { 20 } ifelse  →  1  3 jmp_not_if  10  1 jmp  20
void test_compile_ifelse_expanded(void) {
    CharSource src = make_src_from_string("1 { 10 } { 20 } ifelse }");
    ExecArray *ea = compile_exec_array(&src);

    UT_EQ_INT(7, ea->count);
    UT_EQ_ELEM_INT(1, ea->items[0]);
    UT_EQ_ELEM_INT(3, ea->items[1]);                 // A(1個) + 2
    UT_EQ_INT(ELEM_EXEC_NAME, ea->items[2].type);
    UT_EQ_INT(0, strcmp("jmp_not_if", ea->items[2].u.name));
    UT_EQ_ELEM_INT(10, ea->items[3]);
    UT_EQ_ELEM_INT(1, ea->items[4]);                 // B(1個)
    UT_EQ_INT(ELEM_EXEC_NAME, ea->items[5].type);
    UT_EQ_INT(0, strcmp("jmp", ea->items[5].u.name));
    UT_EQ_ELEM_INT(20, ea->items[6]);

    exec_array_free(ea);
    fclose(src.fp);
}

// 展開したものを実行して、真偽どちらの枝も正しく選ばれること
void test_run_ifelse_expanded(void) {
    int t[] = { 10 };
    int f[] = { 20 };
    check_run_stack("1 { 10 } { 20 } ifelse }", t, 1);
    check_run_stack("0 { 10 } { 20 } ifelse }", f, 1);

    // 枝の中身が複数要素でも、飛ぶ数が正しく計算されること
    int t2[] = { 1, 2, 3 };
    int f2[] = { 4, 5 };
    check_run_stack("1 { 1 2 } { 4 5 } ifelse 3 }", t2, 3);
    check_run_stack("0 { 1 2 } { 4 } ifelse 5 }", f2, 2);
}

// 入れ子の ifelse。内側が先にコンパイル（展開）され、それが外側に
// コピーされる。jmp が相対ジャンプなので、コピーしても飛び先がずれない。
void test_run_ifelse_nested(void) {
    int a[] = { 2 };
    int b[] = { 3 };
    check_run_stack("1 { 0 { 1 } { 2 } ifelse } { 3 } ifelse }", a, 1);
    check_run_stack("0 { 0 { 1 } { 2 } ifelse } { 3 } ifelse }", b, 1);
}

// 直前の2要素が { } でない場合は、コンパイル時に中身が分からないので
// 展開せず ifelse のまま残す（実行時の prim_ifelse に任せる）。
// ※ 実行まではテストしない。a と書くと辞書の { } は「実行」されてしまい、
//   ブロックを値として取り出すには load が要る（未実装）ため。
void test_compile_ifelse_fallback(void) {
    CharSource src = make_src_from_string("1 a b ifelse }");
    ExecArray *ea = compile_exec_array(&src);

    UT_EQ_INT(4, ea->count);
    UT_EQ_INT(ELEM_EXEC_NAME, ea->items[3].type);
    UT_EQ_INT(0, strcmp("ifelse", ea->items[3].u.name));

    exec_array_free(ea);
    fclose(src.fp);
}

int main(void) {
    test_parse_int_single();
    test_parse_int_with_spaces();
    test_parse_int_multiple();
    test_parse_int_empty();
    test_parse_int_non_digit();
    test_parse_int_max();
    test_parse_int_overflow();
    test_parse_one_int();
    test_parse_one_exec_name();
    test_parse_one_literal_name();
    test_parse_one_open_brace();
    test_parse_one_close_brace();
    test_stack_push_pop();
    test_stack_top_does_not_pop();
    test_eval_dup();
    test_eval_pop();
    test_stack_exch();
    test_eval_push_only();
    test_eval_add();
    test_eval_sub();
    test_eval_mul();
    test_eval_div();
    test_eval_mod();
    test_eval_sub_reversed();
    test_eval_add_twice();
    test_dict_put_get();
    test_dict_put_overwrites();
    test_dict_get_undefined_returns_false();
    test_eval_def_and_use();
    test_dict_many_entries();
    test_exec_array_new();
    test_compile_simple();
    test_eval_exec_array_def_and_call();
    test_compile_nested();
    test_compile_grows_beyond_capacity();
    test_eval_eq();
    test_eval_ne();
    test_eval_lt();
    test_eval_gt();
    test_eval_le();
    test_eval_ge();
    test_eval_exch();
    test_eval_if_true();
    test_eval_if_false();
    test_eval_if_uses_stack_below();
    test_eval_ifelse_true();
    test_eval_ifelse_false();
    test_eval_ifelse_resumes_caller();
    test_parse_one_negative_int();
    test_eval_negative_literal();
    test_eval_abs();
    test_eval_index();
    test_eval_index_does_not_consume();
    test_eval_while();
    test_eval_local();
    test_parse_comment();
    test_contstack_push_pop();
    test_run_add();
    test_run_nested();
    test_run_jmp();
    test_run_jmp_not_if();
    test_compile_ifelse_expanded();
    test_run_ifelse_expanded();
    test_run_ifelse_nested();
    test_compile_ifelse_fallback();
    
    if (g_test_fail_count == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("%d TEST(S) FAILED\n", g_test_fail_count);
        return 1;
    }
}
