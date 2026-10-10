#include "exec_array.h"
#include "cont.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

ExecArray *exec_array_new(int count) {
    ExecArray *array = malloc(sizeof(ExecArray)+sizeof(Element)*count);
    assert(array != NULL);
    array->count = count;
    return array;
}

void exec_array_free(ExecArray *ea) {
    free(ea);
}

// 容量が足りなければ倍にしてから、items[*i] に e を入れて *i を進める。
// realloc で配列の場所が変わることがあるので、新しいポインタを返す。
// 呼び出し側は必ず array = push_item(array, ...) と受け直すこと。
static ExecArray *push_item(ExecArray *array, int *i, int *capacity, Element e) {
    if (*i == *capacity) {
        ExecArray *tmp;

        *capacity *= 2;
        tmp = realloc(array, sizeof(ExecArray) + sizeof(Element) * *capacity);
        assert(tmp != NULL);
        array = tmp;
    }
    array->items[*i] = e;
    (*i)++;
    return array;
}

// ea の要素をすべて末尾に追加する
static ExecArray *push_all(ExecArray *array, int *i, int *capacity, ExecArray *ea) {
    for (int k = 0; k < ea->count; k++) {
        array = push_item(array, i, capacity, ea->items[k]);
    }
    return array;
}

// 直前の2要素が { A } { B } なら、ifelse を jmp の列に展開できる
static bool can_expand_ifelse(ExecArray *array, int i) {
    return i >= 2
        && array->items[i - 2].type == ELEM_EXEC_ARRAY
        && array->items[i - 1].type == ELEM_EXEC_ARRAY;
}

// 直前の2要素が { A } { B } なら、while を jmp の列に展開できる
static bool can_expand_while(ExecArray *array, int i) {
    return i >= 2
        && array->items[i - 2].type == ELEM_EXEC_ARRAY
        && array->items[i - 1].type == ELEM_EXEC_ARRAY;
}


// 12章: ifelse の静的コード生成
//
//   cond { A } { B } ifelse
//     ↓
//   cond  N1 jmp_not_if  A...  N2 jmp  B...
//
//   N1 = A の要素数 + 2   偽のとき「A...  N2 jmp」を飛び越えて B の先頭へ
//   N2 = B の要素数       真のとき A を実行したあと B を飛び越えて終わりへ
//
// cond は ifelse より前にすでに詰まっているので触らない。
// 置き換えるのは直前の { A } { B } の2要素だけ。
//
// 例: 3 5 lt { 100 } { 200 } ifelse
//
//   ifelse を読んだ時点   [ 3, 5, lt, {100}, {200} ]                    i=5
//   *i -= 2 の後          [ 3, 5, lt, (ここから上書き) ]                i=3
//   N1 = 1 + 2            [ 3, 5, lt, 3 ]                               i=4
//   jmp_not_if            [ 3, 5, lt, 3, jmp_not_if ]                   i=5
//   A の中身              [ 3, 5, lt, 3, jmp_not_if, 100 ]              i=6
//   N2 = 1                [ ... 100, 1 ]                                i=7
//   jmp                   [ ... 100, 1, jmp ]                           i=8
//   B の中身              [ 3, 5, lt, 3, jmp_not_if, 100, 1, jmp, 200 ] i=9
//
//   真: jmp_not_if で飛ばず 100 を積み、1 jmp で 200 を飛ばす → 100
//   偽: jmp_not_if で 3 個（100 1 jmp）飛ばして 200 に着地   → 200
static ExecArray *expand_ifelse(ExecArray *array, int *i, int *capacity) {
    ExecArray *a = array->items[*i - 2].u.exec_array;
    ExecArray *b = array->items[*i - 1].u.exec_array;

    *i -= 2;   // { A } { B } の2要素を取り消す（ここから上書きしていく）

    array = push_item(array, i, capacity, element_int(a->count + 2));
    array = push_item(array, i, capacity, element_exec_name("jmp_not_if"));
    array = push_all(array, i, capacity, a);
    array = push_item(array, i, capacity, element_int(b->count));
    array = push_item(array, i, capacity, element_exec_name("jmp"));
    array = push_all(array, i, capacity, b);

    // 中身はコピーしたので、A と B の入れ物はもう要らない
    exec_array_free(a);
    exec_array_free(b);
    return array;
}

// 直前の要素が { A } なら、if を jmp_not_if の列に展開できる
static bool can_expand_if(ExecArray *array, int i) {
    return i >= 1 && array->items[i - 1].type == ELEM_EXEC_ARRAY;
}

// 13章: if の静的コード生成
//
//   cond { A } if
//     ↓
//   cond  N jmp_not_if  A...
//
//   N = A の要素数   偽のとき A を丸ごと飛ばす。else が無いので jmp は要らない
//
// 例: 1 { 10 20 } if  →  1  2 jmp_not_if  10 20
static ExecArray *expand_if(ExecArray *array, int *i, int *capacity) {
    ExecArray *a = array->items[*i - 1].u.exec_array;

    *i -= 1;   // { A } の1要素を取り消す（ここから上書きしていく）

    array = push_item(array, i, capacity, element_int(a->count));
    array = push_item(array, i, capacity, element_exec_name("jmp_not_if"));
    array = push_all(array, i, capacity, a);

    exec_array_free(a);
    return array;
}

// 13章: while の静的コード生成
//
//   { A } { B } while        A = 条件、B = 本体
//     ↓
//   A...  N1 jmp_not_if  B...  N2 jmp
//
//   N1 = B の要素数 + 2              偽のとき「B...  N2 jmp」を飛び越えてループの外へ
//   N2 = -(A + B の要素数 + 4)       本体のあと、展開した列の先頭（A の先頭）へ戻る
//
// 条件と本体が呼び出し元と同じフレームに並ぶので、ローカル変数を共有できる。
//
// 例: { C } { B } while（どちらも1要素）
//
//   items[0] = C
//   items[1] = 3            N1 = 1 + 2      jmp_not_if の次(3) から 3 進んで 6（ループの外）
//   items[2] = jmp_not_if
//   items[3] = B
//   items[4] = -6           N2 = -(1+1+4)   jmp の次(6) から 6 戻って 0（C の先頭）
//   items[5] = jmp
//
// 条件 a と本体 b から上の列を作って末尾に追加する。repeat も使う。
static ExecArray *push_while(ExecArray *array, int *i, int *capacity, ExecArray *a, ExecArray *b) {
    array = push_all(array, i, capacity, a);
    array = push_item(array, i, capacity, element_int(b->count + 2));
    array = push_item(array, i, capacity, element_exec_name("jmp_not_if"));
    array = push_all(array, i, capacity, b);
    array = push_item(array, i, capacity, element_int(-(a->count + b->count + 4)));
    array = push_item(array, i, capacity, element_exec_name("jmp"));
    return array;
}

static ExecArray *expand_while(ExecArray *array, int *i, int *capacity) {
    ExecArray *a = array->items[*i - 2].u.exec_array;
    ExecArray *b = array->items[*i - 1].u.exec_array;

    *i -= 2;   // { A } { B } の2要素を取り消す（ここから上書きしていく）

    array = push_while(array, i, capacity, a, b);

    // 中身はコピーしたので、A と B の入れ物はもう要らない
    exec_array_free(a);
    exec_array_free(b);
    return array;
}

// 直前の要素が { B } なら、repeat を jmp の列に展開できる
static bool can_expand_repeat(ExecArray *array, int i) {
    return i >= 1 && array->items[i - 1].type == ELEM_EXEC_ARRAY;
}

// repeat のカウンタ専用の箱。ユーザーが普段使う番号とぶつからないよう最後の箱にする。
#define REPEAT_COUNTER (LOCALS_MAX - 1)

// 13章: repeat の静的コード生成
//
// repeat は「カウンタを1つ足した while」なので、条件と本体を作って push_while に渡す。
//
//   n { B } repeat
//     ↓ 同じ意味
//   n 7 store_local                                  count = n
//   { 7 load_local 0 gt }                            while (count > 0)
//   { B...  7 load_local 1 sub 7 store_local } while     B; count = count - 1;
//
// n は repeat より前にすでに詰まっているので触らない（実行時にスタックへ積まれる）。
static ExecArray *expand_repeat(ExecArray *array, int *i, int *capacity) {
    ExecArray *b = array->items[*i - 1].u.exec_array;
    ExecArray *cond;
    ExecArray *body;
    int k;

    *i -= 1;   // { B } の1要素を取り消す（ここから上書きしていく）

    // count = n
    array = push_item(array, i, capacity, element_int(REPEAT_COUNTER));
    array = push_item(array, i, capacity, element_exec_name("store_local"));

    // 条件: count > 0
    cond = exec_array_new(4);
    cond->items[0] = element_int(REPEAT_COUNTER);
    cond->items[1] = element_exec_name("load_local");
    cond->items[2] = element_int(0);
    cond->items[3] = element_exec_name("gt");

    // 本体: B...  count = count - 1
    body = exec_array_new(b->count + 6);
    for (k = 0; k < b->count; k++) {
        body->items[k] = b->items[k];
    }
    body->items[k++] = element_int(REPEAT_COUNTER);
    body->items[k++] = element_exec_name("load_local");
    body->items[k++] = element_int(1);
    body->items[k++] = element_exec_name("sub");
    body->items[k++] = element_int(REPEAT_COUNTER);
    body->items[k++] = element_exec_name("store_local");

    array = push_while(array, i, capacity, cond, body);

    exec_array_free(cond);
    exec_array_free(body);
    exec_array_free(b);
    return array;
}


ExecArray* compile_exec_array(CharSource *src) {
    Token out_token;
    int capacity = 8;
    int i = 0;

    ExecArray *array = exec_array_new(capacity);
    while (parse_one(src, &out_token)) {
        switch (out_token.type) {
            case TOKEN_OPEN_BRACE: {
                ExecArray *inner = compile_exec_array(src);
                array = push_item(array, &i, &capacity, element_exec_array(inner));
                break;
            }
            case TOKEN_INT:
                array = push_item(array, &i, &capacity, element_int(out_token.u.ival));
                break;
            case TOKEN_LITERAL_NAME:
                array = push_item(array, &i, &capacity, element_literal_name(out_token.u.name));
                break;
            case TOKEN_EXEC_NAME:
                // { A } if、{ A } { B } ifelse / while、{ B } repeat はコンパイル時に jmp の列へ展開する。
                // ブロックが変数経由で渡されるなど、コンパイル時に中身が分からない場合は、
                // 名前のまま残して実行時の prim_if / prim_ifelse / prim_while / prim_repeat に任せる。
                if (strcmp(out_token.u.name, "if") == 0 && can_expand_if(array, i)) {
                    array = expand_if(array, &i, &capacity);
                } else if (strcmp(out_token.u.name, "ifelse") == 0 && can_expand_ifelse(array, i)) {
                    array = expand_ifelse(array, &i, &capacity);
                } else if (strcmp(out_token.u.name, "while") == 0 && can_expand_while(array, i)) {
                    array = expand_while(array, &i, &capacity);
                } else if (strcmp(out_token.u.name, "repeat") == 0 && can_expand_repeat(array, i)) {
                    array = expand_repeat(array, &i, &capacity);
                } else {
                    array = push_item(array, &i, &capacity, element_exec_name(out_token.u.name));
                }
                break;
            case TOKEN_CLOSE_BRACE:
                array->count = i;
                return array;
            default:
                break;
        }
    }
    return NULL;
}
