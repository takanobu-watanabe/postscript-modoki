#ifndef CONT_H
#define CONT_H

#include <stdbool.h>

#include "exec_array.h"

// 継続（continuation）＝「今の処理が終わったら次に何をするか」。
// この教材では「どの実行可能配列を、何番目まで実行したか」で表す。
//
// これまでは同じ情報が eval_exec_array の for ループの変数 i として
// Cのスタックフレームの中に隠れていた。それを明示的なデータにしたもの。
#define LOCALS_MAX 8

typedef struct {
    ExecArray *ea;              // 実行中の実行可能配列
    int pc;                     // 次に実行する items の添字（program counter）
    int locals[LOCALS_MAX];     // ローカル変数（このフレーム専用）
} Continuation;

// 継続スタック。中身は cont.c に隠す。
typedef struct ContStack ContStack;

ContStack *contstack_new(void);
void contstack_free(ContStack *cs);

// ea を pc=0 の状態で積む（＝「これから ea を先頭から実行する」）
void contstack_push(ContStack *cs, ExecArray *ea);

// てっぺんのフレームを返す。呼び出し側が pc を進めるのでポインタで返す。
// 空のときに呼んではいけない。
Continuation *contstack_top(ContStack *cs);

// てっぺんのフレームを捨てる（＝呼び出し元に戻る）
void contstack_pop(ContStack *cs);

bool contstack_is_empty(ContStack *cs);

// 下から i 番目（0 が底）のフレームを返す。表示・デバッグ用。
Continuation *contstack_at(ContStack *cs, int i);
int contstack_size(ContStack *cs);

#endif
