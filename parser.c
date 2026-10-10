#include "parser.h"
#include <limits.h>

// 1文字読む。全角スペース（UTF-8 で E3 80 80 の3バイト）は半角スペース ' ' として返すので、
// 呼び出し側は is_space などの1バイトの判定をそのまま使える。
//
// E3 で始まる全角スペース以外の文字は、ASCII でない文字としてエラーになり読み取りが
// 止まるので、先読みしたバイトを戻しきれなくても困らない（ungetc は1バイトしか保証されない）。
int  cl_getc(CharSource *src) {
    int c = fgetc(src->fp);
    int c2;

    if (c != 0xE3) {
        return c;
    }
    c2 = fgetc(src->fp);
    if (c2 != 0x80) {
        ungetc(c2, src->fp);
        return c;
    }
    if (fgetc(src->fp) == 0x80) {
        return ' ';
    }
    return c;
}

void cl_ungetc(CharSource *src, int c){
    ungetc(c, src->fp);
}

static bool is_space(int c) {
    return c == ' ' || c == '\n' || c == '\t';
}

static bool is_number(int c) {
    return (c >= '0' && c <= '9');
}

static bool is_brace(int c) {
    return c == '{' || c == '}';
}

// 全角文字など ASCII でない文字（UTF-8 では 0x80 以上のバイトで表される）
static bool is_non_ascii(int c) {
    return c >= 0x80;
}

bool parse_int(CharSource *src, int *out) {
    // fgetc の戻り値は、文字だけでなく EOF も区別できる int で受け取る。
    int c;
    int val = 0;
    // 数字を読んだかを記録し、値が 0 の場合と数字がない場合を区別する。
    bool has_digit = false;

    while((c = cl_getc(src)) != EOF) {
        if (is_space(c)){
            if (has_digit) {
                // 数字の後の空白は区切りとして消費し、この整数の読み取りを終える。
                break;
            } else {
                // 数字を読み始める前の空白は読み飛ばす。
                continue;
            }
        }
        if (is_number(c)) {
            // 文字 '0'〜'9' を数値 0〜9 に変換する。
            int digit = c - '0';
            // val * 10 + digit が INT_MAX を超えるか、計算する前に判定する。
            // 失敗時は *out を変更しない。ここまで読んだ入力は消費済み。
            if (val > (INT_MAX - digit) / 10) {
                return false;
            }
            // それまでの値を1桁左へずらし、今回の数字を末尾に加える。
            val = val * 10 + (c - '0');
            has_digit = true;
        } else {
            // 空白でも数字でもない文字は、次の処理が読めるように戻す。
            cl_ungetc(src, c);
            break;
        }
    }
    // 1桁以上読めた場合だけ、呼び出し元へ値を渡す。
    if (has_digit) {
        *out = val;
        return true;
    } else {
        return false;
    }
}

static bool is_literal(int c) {
    if (c == '/')  {
        return true;
    } else {
        return false;
    }
}

static bool is_name(int c) {
    if (c >= 'a' && c <= 'z') {
        return true;
    } else {
        return false;
    }
}

bool parse_one(CharSource *src, Token *out_token) {
    bool b;
    int c; 
    int num;
    bool is_literal_name = false;

    // 空白と、% から行末までのコメントを読み飛ばす。
    // コメントの後にまた空白やコメントが続くことがあるので外側でも繰り返す。
    for (;;) {
        while ((c = cl_getc(src)) != EOF && is_space(c)) {
        }
        if (c != '%') {
            break;
        }
        while ((c = cl_getc(src)) != EOF && c != '\n') {
        }
        if (c == EOF) {
            break;
        }
    }

    if (is_literal(c)) {
       is_literal_name = true;
       c = cl_getc(src);
    }
    if (is_brace(c)) {
        if (c == '{') {
            out_token->type = TOKEN_OPEN_BRACE;
        } else {
            out_token->type = TOKEN_CLOSE_BRACE;
        }
        return true;
    }
    else if (c == '-') {
        int next = cl_getc(src);

        if (is_number(next)) {
            cl_ungetc(src, next);
            b = parse_int(src, &num);
            if (b) {
                out_token->type = TOKEN_INT;
                out_token->u.ival = -num;      // ここで符号を反転
                return true;
            }
            return false;
        }
        // 数字が続かない場合（「- 」や「-abc」）は今回は扱わない
        cl_ungetc(src, next);
        return false;
    }
    else if (is_number(c)) {
        cl_ungetc(src, c);
        b = parse_int(src, &num);
        if (b) {
            out_token->type = TOKEN_INT;
            out_token->u.ival = num;
        } else {
            return false;
        }
        return true;
    }
    else if (is_name(c)) {
        char s[NAME_MAX_SIZE];
        int i = 0;

        // ASCII でない文字も名前の区切りにする（名前に混ぜず、次の parse_one でエラーにする）
        while(!is_space(c) && !is_brace(c) && !is_non_ascii(c) && c!=EOF) {
            if (i==NAME_MAX_SIZE-1) {
                break;
            }
            s[i] = c;
            i++;
            c = cl_getc(src); 
        }
        s[i] = '\0';
        cl_ungetc(src, c);
        if (is_literal_name) {
            out_token->type = TOKEN_LITERAL_NAME;
        } else {
            out_token->type = TOKEN_EXEC_NAME;
        }
        strcpy(out_token->u.name, s);
        return true;
    }
    else if (c == EOF) {
        out_token->type = TOKEN_EOF;
        return false;
    }
    if (is_non_ascii(c)) {
        // 1バイトだけ '%c' で出すと文字化けするので、説明と16進で出す
        fprintf(stderr, "全角文字など ASCII 以外の文字は使えません（0x%02X）。"
                        "全角スペースは空白として扱います\n", c);
        return false;
    }
    fprintf(stderr, "unexpected character: '%c'\n", c);
    return false;
}

void parser_print_all(CharSource *src) {
    Token out_token;

    while (parse_one(src, &out_token)) {
        switch (out_token.type) {
            case TOKEN_LITERAL_NAME:
                printf("LIT %s\n", out_token.u.name);
                break;
            case TOKEN_INT:
                printf("INT %d\n", out_token.u.ival);
                break;
            case TOKEN_EXEC_NAME:
                printf("EXEC %s\n", out_token.u.name);
                break;
            case TOKEN_OPEN_BRACE:
                printf("open_brace\n");
                break;
            case TOKEN_CLOSE_BRACE:
                printf("close_brace\n");
                break;
            default:
                break;
        }
    }
}