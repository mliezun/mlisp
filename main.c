#include "mpc.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef __EMSCRIPTEN__

#include <emscripten/emscripten.h>

#elif _WIN32
/* If we are compiling on Windows compile these functions */
#include <string.h>

static char buffer[2048];

/* Fake readline function */
char *readline(char *prompt) {
  fputs(prompt, stdout);
  fgets(buffer, 2048, stdin);
  char *cpy = malloc(strlen(buffer) + 1);
  strcpy(cpy, buffer);
  cpy[strlen(cpy) - 1] = '\0';
  return cpy;
}

/* Fake add_history function */
void add_history(char *unused) {}

/* Otherwise include the editline headers */
#else
#include <editline/readline.h>
#endif

#define LASSERT(args, cond, fmt, ...)                                          \
  if (!(cond)) {                                                               \
    lval *err = lval_err(fmt, ##__VA_ARGS__);                                  \
    lval_del(args);                                                            \
    return err;                                                                \
  }
#define LASSERT_NUM(func, a, n)                                                \
  LASSERT(a, a->count == n,                                                    \
          "Function '%s' passed too many arguments. Got %i, Expected %i.",     \
          func, a->count, n)
#define LASSERT_TYPE(func, a, i, tp)                                           \
  LASSERT(a, a->cell[i]->type == tp,                                           \
          "Function '%s' passed incorrect type. Got %s, Expected %s.", func,   \
          ltype_name(a->cell[i]->type), ltype_name(tp))

/* Forward Declarations */
struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;
char *lval_to_str(lval *v);
void lval_print(lval *v);
lval *lval_eval(lenv *e, lval *v);
void lenv_del(lenv *e);
lenv *lenv_copy(lenv *e);
mpc_parser_t *Number;
mpc_parser_t *Symbol;
mpc_parser_t *String;
mpc_parser_t *Comment;
mpc_parser_t *Sexpr;
mpc_parser_t *Qexpr;
mpc_parser_t *Expr;
mpc_parser_t *Mlisp;
int mlisp_init();
lenv *globalEnv;

/* Enumeration of Possible lval Types */
enum {
  LVAL_ERR,
  LVAL_NUM,
  LVAL_SYM,
  LVAL_STR,
  LVAL_FUN,
  LVAL_SEXPR,
  LVAL_QEXPR
};

/* Builtin function pointer */
typedef lval *(*lbuiltin)(lenv *, lval *);

/* Struct that holds a Lisp value */
struct lval {
  int type;

  /* Basic */
  long num;
  char *err;
  char *sym;
  char *str;

  /* Function */
  lbuiltin builtin;
  lenv *env;
  lval *formals;
  lval *body;

  /* Expression */
  int count;
  lval **cell;
};

/* Struct that holds an environment */
struct lenv {
  lenv *par;
  int count;
  char **syms;
  lval **vals;
};

char *ltype_name(int t) {
  switch (t) {
  case LVAL_FUN:
    return "Function";
  case LVAL_NUM:
    return "Number";
  case LVAL_ERR:
    return "Error";
  case LVAL_SYM:
    return "Symbol";
  case LVAL_STR:
    return "String";
  case LVAL_SEXPR:
    return "S-Expression";
  case LVAL_QEXPR:
    return "Q-Expression";
  default:
    return "Unknown";
  }
}

/* Initializes environment */
lenv *lenv_new(void) {
  lenv *e = malloc(sizeof(lenv));
  e->par = NULL;
  e->count = 0;
  e->syms = NULL;
  e->vals = NULL;
  return e;
}

/* Construct a pointer to a new Number lval */
lval *lval_num(long x) {
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}

/* Construct a pointer to a new Error lval */
lval *lval_err(char *fmt, ...) {
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_ERR;

  /* Create a va list and initialize it */
  va_list va;
  va_start(va, fmt);

  /* Allocate 512 bytes of space */
  v->err = malloc(512);

  /* printf the error string with a maximum of 511 characters */
  vsnprintf(v->err, 511, fmt, va);

  /* Reallocate to number of bytes actually used */
  v->err = realloc(v->err, strlen(v->err) + 1);

  /* Cleanup our va list */
  va_end(va);

  return v;
}

/* Construct a pointer to a new Symbol lval */
lval *lval_sym(char *s) {
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->sym = malloc(strlen(s) + 1);
  strcpy(v->sym, s);
  return v;
}

/* A pointer to a new empty Sexpr lval */
lval *lval_sexpr(void) {
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_SEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

/* A pointer to a new empty Qexpr lval */
lval *lval_qexpr(void) {
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_QEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

/* A pointer to a new empty String lval */
lval *lval_str(char *s) {
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_STR;
  v->str = malloc(strlen(s) + 1);
  strcpy(v->str, s);
  return v;
}

/* A pointer to a new empty Function lval */
lval *lval_fun(lbuiltin func) {
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_FUN;
  v->builtin = func;
  return v;
}

lval *lval_lambda(lval *formals, lval *body) {
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_FUN;

  /* Set Builtin to Null */
  v->builtin = NULL;

  /* Build new environment */
  v->env = lenv_new();

  /* Set Formals and Body */
  v->formals = formals;
  v->body = body;
  return v;
}

lval *lval_copy(lval *v) {

  lval *x = malloc(sizeof(lval));
  x->type = v->type;

  switch (v->type) {

  /* Copy Functions and Numbers Directly */
  case LVAL_FUN:
    if (v->builtin) {
      x->builtin = v->builtin;
    } else {
      x->builtin = NULL;
      x->env = lenv_copy(v->env);
      x->formals = lval_copy(v->formals);
      x->body = lval_copy(v->body);
    }
    break;
  case LVAL_NUM:
    x->num = v->num;
    break;

  /* Copy Strings using malloc and strcpy */
  case LVAL_ERR:
    x->err = malloc(strlen(v->err) + 1);
    strcpy(x->err, v->err);
    break;

  case LVAL_SYM:
    x->sym = malloc(strlen(v->sym) + 1);
    strcpy(x->sym, v->sym);
    break;

  case LVAL_STR:
    x->str = malloc(strlen(v->str) + 1);
    strcpy(x->str, v->str);
    break;

  /* Copy Lists by copying each sub-expression */
  case LVAL_SEXPR:
  case LVAL_QEXPR:
    x->count = v->count;
    x->cell = malloc(sizeof(lval *) * x->count);
    for (int i = 0; i < x->count; i++) {
      x->cell[i] = lval_copy(v->cell[i]);
    }
    break;
  }

  return x;
}

lenv *lenv_copy(lenv *e) {
  lenv *n = malloc(sizeof(lenv));
  n->par = e->par;
  n->count = e->count;
  n->syms = malloc(sizeof(char *) * n->count);
  n->vals = malloc(sizeof(lval *) * n->count);
  for (int i = 0; i < e->count; i++) {
    n->syms[i] = malloc(strlen(e->syms[i]) + 1);
    strcpy(n->syms[i], e->syms[i]);
    n->vals[i] = lval_copy(e->vals[i]);
  }
  return n;
}

void lval_del(lval *v) {

  switch (v->type) {
  /* Do nothing special for number type */
  case LVAL_NUM:
    break;

  /* For Err or Sym free the string data */
  case LVAL_ERR:
    free(v->err);
    break;
  case LVAL_SYM:
    free(v->sym);
    break;

  case LVAL_STR:
    free(v->str);
    break;

  /* If Qexpr or Sexpr then delete all elements inside */
  case LVAL_QEXPR:
  case LVAL_SEXPR:
    for (int i = 0; i < v->count; i++) {
      lval_del(v->cell[i]);
    }
    /* Also free the memory allocated to contain the pointers */
    free(v->cell);
    break;

  case LVAL_FUN:
    if (!v->builtin) {
      lenv_del(v->env);
      lval_del(v->formals);
      lval_del(v->body);
    }
    break;
  }

  /* Free the memory allocated for the "lval" struct itself */
  free(v);
}

void lenv_del(lenv *e) {
  for (int i = 0; i < e->count; i++) {
    free(e->syms[i]);
    lval_del(e->vals[i]);
  }
  free(e->syms);
  free(e->vals);
  free(e);
}

lval *lenv_get(lenv *e, lval *k) {

  /* Iterate over all items in environment */
  for (int i = 0; i < e->count; i++) {
    /* Check if the stored string matches the symbol string */
    /* If it does, return a copy of the value */
    if (strcmp(e->syms[i], k->sym) == 0) {
      return lval_copy(e->vals[i]);
    }
  }

  /* If no symbol check in parent otherwise error */
  if (e->par) {
    return lenv_get(e->par, k);
  } else {
    return lval_err("Unbound Symbol '%s'", k->sym);
  }
}

void lenv_put(lenv *e, lval *k, lval *v) {

  /* Iterate over all items in environment */
  /* This is to see if variable already exists */
  for (int i = 0; i < e->count; i++) {

    /* If variable is found delete item at that position */
    /* And replace with variable supplied by user */
    if (strcmp(e->syms[i], k->sym) == 0) {
      lval_del(e->vals[i]);
      e->vals[i] = lval_copy(v);
      return;
    }
  }

  /* If no existing entry found allocate space for new entry */
  e->count++;
  e->vals = realloc(e->vals, sizeof(lval *) * e->count);
  e->syms = realloc(e->syms, sizeof(char *) * e->count);

  /* Copy contents of lval and symbol string into new location */
  e->vals[e->count - 1] = lval_copy(v);
  e->syms[e->count - 1] = malloc(strlen(k->sym) + 1);
  strcpy(e->syms[e->count - 1], k->sym);
}

void lenv_def(lenv *e, lval *k, lval *v) {
  /* Iterate till e has no parent */
  while (e->par) {
    e = e->par;
  }
  /* Put value in e */
  lenv_put(e, k, v);
}

lval *lval_read_num(mpc_ast_t *t) {
  errno = 0;
  long x = strtol(t->contents, NULL, 10);
  return errno != ERANGE ? lval_num(x)
                         : lval_err("Invalid number. Got '%s'.", t->contents);
}

lval *lval_read_str(mpc_ast_t *t) {
  /* Cut off the final quote character */
  t->contents[strlen(t->contents) - 1] = '\0';
  /* Copy the string missing out the first quote character */
  char *unescaped = malloc(strlen(t->contents + 1) + 1);
  strcpy(unescaped, t->contents + 1);
  /* Pass through the unescape function */
  unescaped = mpcf_unescape(unescaped);
  /* Construct a new lval using the string */
  lval *str = lval_str(unescaped);
  /* Free the string and return */
  free(unescaped);
  return str;
}

lval *lval_add(lval *v, lval *x) {
  v->count++;
  v->cell = realloc(v->cell, sizeof(lval *) * v->count);
  v->cell[v->count - 1] = x;
  return v;
}

lval *lval_read(mpc_ast_t *t) {

  /* If Symbol or Number return conversion to that type */
  if (strstr(t->tag, "number")) {
    return lval_read_num(t);
  }
  if (strstr(t->tag, "symbol")) {
    return lval_sym(t->contents);
  }

  if (strstr(t->tag, "string")) {
    return lval_read_str(t);
  }

  /* If root (>) or sexpr then create empty list */
  lval *x = NULL;
  if (strcmp(t->tag, ">") == 0) {
    x = lval_sexpr();
  }
  if (strstr(t->tag, "sexpr")) {
    x = lval_sexpr();
  }
  if (strstr(t->tag, "qexpr")) {
    x = lval_qexpr();
  }

  /* Fill this list with any valid expression contained within */
  for (int i = 0; i < t->children_num; i++) {
    if (strcmp(t->children[i]->contents, "(") == 0) {
      continue;
    }
    if (strcmp(t->children[i]->contents, ")") == 0) {
      continue;
    }
    if (strcmp(t->children[i]->contents, "}") == 0) {
      continue;
    }
    if (strcmp(t->children[i]->contents, "{") == 0) {
      continue;
    }
    if (strcmp(t->children[i]->tag, "regex") == 0) {
      continue;
    }
    if (strstr(t->children[i]->tag, "comment")) {
      continue;
    }
    x = lval_add(x, lval_read(t->children[i]));
  }

  return x;
}

char *lval_expr_to_str(lval *v, char open, char close) {
  char *out = malloc(sizeof(char) * 2);
  out[0] = open;
  out[1] = '\0';
  for (int i = 0; i < v->count; i++) {

    char *str = lval_to_str(v->cell[i]);

    out = realloc(out, sizeof(char) * (strlen(out) + strlen(str) + 3));

    strcat(out, str);

    /* Don't print trailing space if last element */
    if (i != (v->count - 1)) {
      strcat(out, " ");
    }
  }
  char closing[] = {close, '\0'};
  strcat(out, closing);
  return out;
}

char *lval_escape_str(lval *v) {
  /* Make a Copy of the string */
  char *escaped = malloc(strlen(v->str) + 1);
  strcpy(escaped, v->str);
  /* Pass it through the escape function */
  escaped = mpcf_escape(escaped);
  return escaped;
}

char *lval_to_str(lval *v) {
  char *out;
  switch (v->type) {
  case LVAL_NUM: {
    out = malloc(sizeof(int) * 8 + 1);
    sprintf(out, "%li", v->num);
    return out;
  }
  case LVAL_ERR: {
    char *error = "Error: %s";
    out = malloc(sizeof(char) * (strlen(error) + strlen(v->err) + 1));
    sprintf(out, error, v->err);
    return out;
  }
  case LVAL_SYM: {
    out = malloc(sizeof(char) * (strlen(v->sym) + 1));
    sprintf(out, "%s", v->sym);
    return out;
  }
  case LVAL_STR:
    return lval_escape_str(v);
  case LVAL_SEXPR:
    return lval_expr_to_str(v, '(', ')');
  case LVAL_QEXPR:
    return lval_expr_to_str(v, '{', '}');
  case LVAL_FUN:
    if (v->builtin) {
      char *builtin = "<builtin>";
      out = malloc(sizeof(char) * (strlen(builtin) + 1));
      sprintf(out, "%s", builtin);
    } else {
      char *format = "(\\ %s %s)";
      char *formals = lval_to_str(v->formals);
      char *body = lval_to_str(v->body);
      out = malloc(sizeof(char) *
                   (strlen(format) + strlen(formals) + strlen(body) + 1));
      sprintf(out, format, formals, body);
      free(formals);
      free(body);
    }
    return out;
  }
  return NULL;
}

/* Print an "lval" */
void lval_print(lval *v) {
  char *str = lval_to_str(v);
  printf("%s", str);
  free(str);
}

/* Print an "lval" followed by a newline */
void lval_println(lval *v) {
  lval_print(v);
  putchar('\n');
}

lval *lval_pop(lval *v, int i) {
  /* Find the item at "i" */
  lval *x = v->cell[i];

  /* Shift memory after the item at "i" over the top */
  memmove(&v->cell[i], &v->cell[i + 1], sizeof(lval *) * (v->count - i - 1));

  /* Decrease the count of items in the list */
  v->count--;

  /* Reallocate the memory used */
  v->cell = realloc(v->cell, sizeof(lval *) * v->count);
  return x;
}

lval *lval_take(lval *v, int i) {
  lval *x = lval_pop(v, i);
  lval_del(v);
  return x;
}

lval *builtin_op(lenv *e, lval *a, char *op) {

  /* Ensure all arguments are numbers */
  for (int i = 0; i < a->count; i++) {
    if (a->cell[i]->type != LVAL_NUM) {
      int tp = a->cell[i]->type;
      lval_del(a);
      return lval_err("Function '%s' passed incorrect type for argument %i. "
                      "Got %s, Expected %s.",
                      op, i, ltype_name(tp), ltype_name(LVAL_NUM));
    }
  }

  /* Pop the first element */
  lval *x = lval_pop(a, 0);

  /* If no arguments and sub then perform unary negation */
  if ((strcmp(op, "-") == 0) && a->count == 0) {
    x->num = -x->num;
  }

  /* While there are still elements remaining */
  while (a->count > 0) {

    /* Pop the next element */
    lval *y = lval_pop(a, 0);

    if (strcmp(op, "+") == 0) {
      x->num += y->num;
    }
    if (strcmp(op, "-") == 0) {
      x->num -= y->num;
    }
    if (strcmp(op, "*") == 0) {
      x->num *= y->num;
    }
    if (strcmp(op, "/") == 0) {
      if (y->num == 0) {
        lval_del(x);
        lval_del(y);
        x = lval_err("Division By Zero!");
        break;
      }
      x->num /= y->num;
    }

    lval_del(y);
  }

  lval_del(a);
  return x;
}

lval *builtin_head(lenv *e, lval *a) {
  LASSERT(a, a->count == 1,
          "Function 'head' passed too many arguments. Got %i, "
          "Expected %i.",
          a->count, 1);
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'head' passed incorrect type for argument 0. Got %s, "
          "Expected %s.",
          ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));
  LASSERT(a, a->cell[0]->count != 0, "Function 'head' passed {}.");

  /* Otherwise take first argument */
  lval *v = lval_take(a, 0);

  /* Delete all elements that are not head and return */
  while (v->count > 1) {
    lval_del(lval_pop(v, 1));
  }
  return v;
}

lval *builtin_tail(lenv *e, lval *a) {
  LASSERT(a, a->count == 1,
          "Function 'tail' passed incorrect number of arguments. Got %i, "
          "Expected %i.",
          a->count, 1);
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'tail' passed incorrect type. Got %s, Expected %s.",
          ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));
  LASSERT(a, a->cell[0]->count != 0, "Function 'tail' passed {}!");

  /* Take first argument */
  lval *v = lval_take(a, 0);

  /* Delete first element and return */
  lval_del(lval_pop(v, 0));
  return v;
}

lval *builtin_list(lenv *e, lval *a) {
  a->type = LVAL_QEXPR;
  return a;
}

lval *builtin_eval(lenv *e, lval *a) {
  LASSERT(a, a->count == 1,
          "Function 'eval' passed too many arguments. Got %i, "
          "Expected %i.",
          a->count, 1);
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'eval' passed incorrect type. Got %s, Expected %s.",
          ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));

  lval *x = lval_take(a, 0);
  x->type = LVAL_SEXPR;
  return lval_eval(e, x);
}

lval *lval_join(lval *x, lval *y) {

  /* For each cell in 'y' add it to 'x' */
  while (y->count) {
    x = lval_add(x, lval_pop(y, 0));
  }

  /* Delete the empty 'y' and return 'x' */
  lval_del(y);
  return x;
}

lval *builtin_join(lenv *e, lval *a) {

  for (int i = 0; i < a->count; i++) {
    LASSERT(a, a->cell[i]->type == LVAL_QEXPR,
            "Function 'join' passed incorrect type. Got %s, Expected %s.",
            ltype_name(a->cell[i]->type), ltype_name(LVAL_QEXPR));
  }

  lval *x = lval_pop(a, 0);

  while (a->count) {
    x = lval_join(x, lval_pop(a, 0));
  }

  lval_del(a);
  return x;
}

lval *builtin_add(lenv *e, lval *a) { return builtin_op(e, a, "+"); }

lval *builtin_sub(lenv *e, lval *a) { return builtin_op(e, a, "-"); }

lval *builtin_mul(lenv *e, lval *a) { return builtin_op(e, a, "*"); }

lval *builtin_div(lenv *e, lval *a) { return builtin_op(e, a, "/"); }

lval *builtin_var(lenv *e, lval *a, char *func) {
  LASSERT_TYPE(func, a, 0, LVAL_QEXPR);

  lval *syms = a->cell[0];
  for (int i = 0; i < syms->count; i++) {
    LASSERT(a, (syms->cell[i]->type == LVAL_SYM),
            "Function '%s' cannot define non-symbol. "
            "Got %s, Expected %s.",
            func, ltype_name(syms->cell[i]->type), ltype_name(LVAL_SYM));
  }

  LASSERT(a, (syms->count == a->count - 1),
          "Function '%s' passed too many arguments for symbols. "
          "Got %i, Expected %i.",
          func, syms->count, a->count - 1);

  for (int i = 0; i < syms->count; i++) {
    /* If 'def' define in globally. If 'put' define in locally */
    if (strcmp(func, "def") == 0) {
      lenv_def(e, syms->cell[i], a->cell[i + 1]);
    }

    if (strcmp(func, "=") == 0) {
      lenv_put(e, syms->cell[i], a->cell[i + 1]);
    }
  }

  lval_del(a);
  return lval_sexpr();
}

lval *builtin_def(lenv *e, lval *a) { return builtin_var(e, a, "def"); }

lval *builtin_put(lenv *e, lval *a) { return builtin_var(e, a, "="); }

lval *builtin_lambda(lenv *e, lval *a) {
  /* Check Two arguments, each of which are Q-Expressions */
  LASSERT_NUM("\\", a, 2);
  LASSERT_TYPE("\\", a, 0, LVAL_QEXPR);
  LASSERT_TYPE("\\", a, 1, LVAL_QEXPR);

  /* Check first Q-Expression contains only Symbols */
  for (int i = 0; i < a->cell[0]->count; i++) {
    LASSERT(a, (a->cell[0]->cell[i]->type == LVAL_SYM),
            "Cannot define non-symbol. Got %s, Expected %s.",
            ltype_name(a->cell[0]->cell[i]->type), ltype_name(LVAL_SYM));
  }

  /* Pop first two arguments and pass them to lval_lambda */
  lval *formals = lval_pop(a, 0);
  lval *body = lval_pop(a, 0);
  lval_del(a);

  return lval_lambda(formals, body);
}

lval *builtin_fun(lenv *e, lval *a) {
  LASSERT_NUM("fun", a, 2);
  LASSERT_TYPE("fun", a, 0, LVAL_QEXPR);
  LASSERT_TYPE("fun", a, 1, LVAL_QEXPR);
  LASSERT(a, a->cell[0]->count != 0, "Function name is required.");

  /* Check first Q-Expression contains only Symbols */
  for (int i = 0; i < a->cell[0]->count; i++) {
    LASSERT(a, (a->cell[0]->cell[i]->type == LVAL_SYM),
            "Cannot define non-symbol. Got %s, Expected %s.",
            ltype_name(a->cell[0]->cell[i]->type), ltype_name(LVAL_SYM));
  }

  lval *argList = lval_qexpr();

  lval *nameArgs = lval_pop(a, 0);
  lval *body = lval_pop(a, 0);

  lval_add(argList, nameArgs);

  lval *name = builtin_head(e, lval_copy(argList));
  lval *args = builtin_tail(e, argList);

  lval *lamb = lval_qexpr();
  lamb = lval_add(lamb, args);
  lamb = lval_add(lamb, body);
  lamb = builtin_lambda(e, lamb);

  lval *fun = lval_qexpr();
  fun = lval_add(fun, name);
  fun = lval_add(fun, lamb);
  fun = builtin_def(e, fun);

  lval_del(a);
  return fun;
}

lval *builtin_ord(lenv *e, lval *a, char *func) {
  LASSERT_NUM(func, a, 2);
  LASSERT_TYPE(func, a, 0, LVAL_NUM);
  LASSERT_TYPE(func, a, 1, LVAL_NUM);

  long n1 = a->cell[0]->num;
  long n2 = a->cell[1]->num;

  lval_del(a);

  if (strcmp(func, "<") == 0) {
    return lval_num(n1 < n2);
  }
  if (strcmp(func, "<=") == 0) {
    return lval_num(n1 <= n2);
  }

  if (strcmp(func, ">") == 0) {
    return lval_num(n1 > n2);
  }
  if (strcmp(func, ">=") == 0) {
    return lval_num(n1 >= n2);
  }

  return lval_err("Undefined operator: '%s'", func);
}

lval *builtin_lt(lenv *e, lval *a) { return builtin_ord(e, a, "<"); }
lval *builtin_lte(lenv *e, lval *a) { return builtin_ord(e, a, "<="); }
lval *builtin_gt(lenv *e, lval *a) { return builtin_ord(e, a, ">"); }
lval *builtin_gte(lenv *e, lval *a) { return builtin_ord(e, a, ">="); }

int lval_eq(lval *x, lval *y) {

  /* Different Types are always unequal */
  if (x->type != y->type) {
    return 0;
  }

  /* Compare Based upon type */
  switch (x->type) {
  /* Compare Number Value */
  case LVAL_NUM:
    return (x->num == y->num);

  /* Compare String Values */
  case LVAL_ERR:
    return (strcmp(x->err, y->err) == 0);
  case LVAL_SYM:
    return (strcmp(x->sym, y->sym) == 0);

  /* If builtin compare, otherwise compare formals and body */
  case LVAL_FUN:
    if (x->builtin || y->builtin) {
      return x->builtin == y->builtin;
    } else {
      return lval_eq(x->formals, y->formals) && lval_eq(x->body, y->body);
    }

  case LVAL_STR:
    return (strcmp(x->str, y->str) == 0);

  /* If list compare every individual element */
  case LVAL_QEXPR:
  case LVAL_SEXPR:
    if (x->count != y->count) {
      return 0;
    }
    for (int i = 0; i < x->count; i++) {
      /* If any element not equal then whole list not equal */
      if (!lval_eq(x->cell[i], y->cell[i])) {
        return 0;
      }
    }
    /* Otherwise lists must be equal */
    return 1;
    break;
  }
  return 0;
}

lval *builtin_cmp(lenv *e, lval *a, char *op) {
  LASSERT_NUM(op, a, 2);
  int r;
  if (strcmp(op, "==") == 0) {
    r = lval_eq(a->cell[0], a->cell[1]);
  }
  if (strcmp(op, "!=") == 0) {
    r = !lval_eq(a->cell[0], a->cell[1]);
  }
  lval_del(a);
  return lval_num(r);
}

lval *builtin_eq(lenv *e, lval *a) { return builtin_cmp(e, a, "=="); }

lval *builtin_ne(lenv *e, lval *a) { return builtin_cmp(e, a, "!="); }

lval *builtin_bool(lenv *e, lval *a, char *func) {
  LASSERT(a, a->count >= 2,
          "Boolean operation '%s' takes at least 2 arguments.", func);

  int isAnd = (strcmp(func, "&&") == 0);
  int r = (isAnd ? 1 : 0);

  for (int i = 0; i < a->count; i++) {
    a->cell[i] = lval_eval(e, a->cell[i]);
    LASSERT_TYPE(func, a, i, LVAL_NUM);
    if (isAnd) {
      if (!a->cell[i]->num) {
        r = 0;
        break;
      }
    } else {
      if (a->cell[i]->num) {
        r = 1;
        break;
      }
    }
  }

  lval_del(a);

  return lval_num(r);
}

lval *builtin_and(lenv *e, lval *a) { return builtin_bool(e, a, "&&"); }

lval *builtin_or(lenv *e, lval *a) { return builtin_bool(e, a, "||"); }

lval *builtin_not(lenv *e, lval *a) {
  LASSERT_NUM("!", a, 1);
  a = lval_eval(e, a);
  LASSERT(a, a->type == LVAL_NUM,
          "Function '%s' passed incorrect type. Got %s, Expected %s.", "!",
          ltype_name(a->type), LVAL_NUM);
  int r = !a->num;
  lval_del(a);
  return lval_num(r);
}

lval *builtin_if(lenv *e, lval *a) {
  LASSERT_NUM("if", a, 3);
  LASSERT_TYPE("if", a, 0, LVAL_NUM);
  LASSERT_TYPE("if", a, 1, LVAL_QEXPR);
  LASSERT_TYPE("if", a, 2, LVAL_QEXPR);

  /* Mark Both Expressions as evaluable */
  lval *x;
  a->cell[1]->type = LVAL_SEXPR;
  a->cell[2]->type = LVAL_SEXPR;

  if (a->cell[0]->num) {
    /* If condition is true evaluate first expression */
    x = lval_eval(e, lval_pop(a, 1));
  } else {
    /* Otherwise evaluate second expression */
    x = lval_eval(e, lval_pop(a, 2));
  }

  /* Delete argument list and return */
  lval_del(a);
  return x;
}

lval *builtin_load(lenv *e, lval *a) {
  LASSERT_NUM("load", a, 1);
  LASSERT_TYPE("load", a, 0, LVAL_STR);

  /* Parse File given by string name */
  mpc_result_t r;
  if (mpc_parse_contents(a->cell[0]->str, Mlisp, &r)) {

    /* Read contents */
    lval *expr = lval_read(r.output);
    mpc_ast_delete(r.output);

    /* Evaluate each Expression */
    while (expr->count) {
      lval *x = lval_eval(e, lval_pop(expr, 0));
      /* If Evaluation leads to error print it */
      if (x->type == LVAL_ERR) {
        lval_println(x);
      }
      lval_del(x);
    }

    /* Delete expressions and arguments */
    lval_del(expr);
    lval_del(a);

    /* Return empty list */
    return lval_sexpr();

  } else {
    /* Get Parse Error as String */
    char *err_msg = mpc_err_string(r.error);
    mpc_err_delete(r.error);

    /* Create new error message using it */
    lval *err = lval_err("Could not load Library %s", err_msg);
    free(err_msg);
    lval_del(a);

    /* Cleanup and return error */
    return err;
  }
}

lval *builtin_print(lenv *e, lval *a) {
#if __EMSCRIPTEN__
  char *out = malloc(sizeof(char));
  out[0] = '\0';
  /* Concat each argument followed by a space */
  for (int i = 0; i < a->count; i++) {
    char *str = lval_to_str(a->cell[i]);
    out = realloc(out, sizeof(char) * (strlen(out) + strlen(str) + 2));
    strcat(out, str);
    strcat(out, " ");
    free(str);
  }
  lval *x = lval_str(out);
  free(out);
  return x;
#else
  /* Print each argument followed by a space */
  for (int i = 0; i < a->count; i++) {
    lval_print(a->cell[i]);
    putchar(' ');
  }

  /* Print a newline and delete arguments */
  putchar('\n');
  lval_del(a);

  return lval_sexpr();
#endif
}

lval *builtin_error(lenv *e, lval *a) {
  LASSERT_NUM("error", a, 1);
  LASSERT_TYPE("error", a, 0, LVAL_STR);

  /* Construct Error from first argument */
  lval *err = lval_err(a->cell[0]->str);

  /* Delete arguments and return */
  lval_del(a);
  return err;
}

lval *lval_call(lenv *e, lval *f, lval *a) {

  /* If Builtin then simply apply that */
  if (f->builtin) {
    return f->builtin(e, a);
  }

  /* Record Argument Counts */
  int given = a->count;
  int total = f->formals->count;

  /* While arguments still remain to be processed */
  while (a->count) {

    /* If we've ran out of formal arguments to bind */
    if (f->formals->count == 0) {
      lval_del(a);
      return lval_err("Function passed too many arguments. "
                      "Got %i, Expected %i.",
                      given, total);
    }

    /* Pop the first symbol from the formals */
    lval *sym = lval_pop(f->formals, 0);

    /* Special Case to deal with '&' */
    if (strcmp(sym->sym, "&") == 0) {

      /* Ensure '&' is followed by another symbol */
      if (f->formals->count != 1) {
        lval_del(a);
        return lval_err("Function format invalid. "
                        "Symbol '&' not followed by single symbol.");
      }

      /* Next formal should be bound to remaining arguments */
      lval *nsym = lval_pop(f->formals, 0);
      lenv_put(f->env, nsym, builtin_list(e, a));
      lval_del(sym);
      lval_del(nsym);
      break;
    }

    /* Pop the next argument from the list */
    lval *val = lval_pop(a, 0);

    /* Bind a copy into the function's environment */
    lenv_put(f->env, sym, val);

    /* Delete symbol and value */
    lval_del(sym);
    lval_del(val);
  }

  /* Argument list is now bound so can be cleaned up */
  lval_del(a);

  /* If '&' remains in formal list bind to empty list */
  if (f->formals->count > 0 && strcmp(f->formals->cell[0]->sym, "&") == 0) {

    /* Check to ensure that & is not passed invalidly. */
    if (f->formals->count != 2) {
      return lval_err("Function format invalid. "
                      "Symbol '&' not followed by single symbol.");
    }

    /* Pop and delete '&' symbol */
    lval_del(lval_pop(f->formals, 0));

    /* Pop next symbol and create empty list */
    lval *sym = lval_pop(f->formals, 0);
    lval *val = lval_qexpr();

    /* Bind to environment and delete */
    lenv_put(f->env, sym, val);
    lval_del(sym);
    lval_del(val);
  }

  /* If all formals have been bound evaluate */
  if (f->formals->count == 0) {

    /* Set environment parent to evaluation environment */
    f->env->par = e;

    /* Evaluate and return */
    return builtin_eval(f->env, lval_add(lval_sexpr(), lval_copy(f->body)));
  } else {
    /* Otherwise return partially evaluated function */
    return lval_copy(f);
  }
}

lval *lval_eval_sexpr(lenv *e, lval *v) {

  /* Evaluate Children */
  for (int i = 0; i < v->count; i++) {
    v->cell[i] = lval_eval(e, v->cell[i]);
  }

  /* Error Checking */
  for (int i = 0; i < v->count; i++) {
    if (v->cell[i]->type == LVAL_ERR) {
      return lval_take(v, i);
    }
  }

  /* Empty Expression */
  if (v->count == 0) {
    return v;
  }

  /* Single Expression */
  if (v->count == 1) {
    return lval_take(v, 0);
  }

  /* Ensure first element is a function after evaluation */
  lval *f = lval_pop(v, 0);
  if (f->type != LVAL_FUN) {
    lval *err = lval_err("S-Expression starts with incorrect type. "
                         "Got %s, Expected %s.",
                         ltype_name(f->type), ltype_name(LVAL_FUN));
    lval_del(f);
    lval_del(v);
    return err;
  }

  /* Call function to get result */
  lval *result = lval_call(e, f, v);
  lval_del(f);
  return result;
}

lval *lval_eval(lenv *e, lval *v) {
  if (v->type == LVAL_SYM) {
    lval *x = lenv_get(e, v);
    lval_del(v);
    return x;
  }
  /* Evaluate Sexpressions */
  if (v->type == LVAL_SEXPR) {
    return lval_eval_sexpr(e, v);
  }
  /* All other lval types remain the same */
  return v;
}

void lenv_add_builtin(lenv *e, char *name, lbuiltin func) {
  lval *k = lval_sym(name);
  lval *v = lval_fun(func);
  lenv_put(e, k, v);
  lval_del(k);
  lval_del(v);
}

void lenv_add_builtins(lenv *e) {
  /* List Functions */
  lenv_add_builtin(e, "list", builtin_list);
  lenv_add_builtin(e, "head", builtin_head);
  lenv_add_builtin(e, "tail", builtin_tail);
  lenv_add_builtin(e, "eval", builtin_eval);
  lenv_add_builtin(e, "join", builtin_join);

  /* Mathematical Functions */
  lenv_add_builtin(e, "+", builtin_add);
  lenv_add_builtin(e, "-", builtin_sub);
  lenv_add_builtin(e, "*", builtin_mul);
  lenv_add_builtin(e, "/", builtin_div);

  /* Variable Functions */
  lenv_add_builtin(e, "def", builtin_def);
  lenv_add_builtin(e, "=", builtin_put);
  lenv_add_builtin(e, "\\", builtin_lambda);
  lenv_add_builtin(e, "fun", builtin_fun);

  /* Comparison Functions */
  lenv_add_builtin(e, "<", builtin_lt);
  lenv_add_builtin(e, "<=", builtin_lte);
  lenv_add_builtin(e, ">", builtin_gt);
  lenv_add_builtin(e, ">=", builtin_gte);
  lenv_add_builtin(e, "==", builtin_eq);
  lenv_add_builtin(e, "!=", builtin_ne);
  lenv_add_builtin(e, "&&", builtin_and);
  lenv_add_builtin(e, "||", builtin_or);
  lenv_add_builtin(e, "!", builtin_not);
  lenv_add_builtin(e, "if", builtin_if);

  /* String Functions */
  lenv_add_builtin(e, "load", builtin_load);
  lenv_add_builtin(e, "error", builtin_error);
  lenv_add_builtin(e, "print", builtin_print);
}

extern const int stdlib_mlisp_size;
extern const char stdlib_mlisp[];

int init_stdlib(lenv *e) {
  /* Attempt to Parse the user Input */
  mpc_result_t r;
  if (mpc_nparse("<stdlib>", stdlib_mlisp, stdlib_mlisp_size, Mlisp, &r)) {
    lval *x = lval_eval(e, lval_read(r.output));
    lval_del(x);
    mpc_ast_delete(r.output);
  } else {
    /* Otherwise Print the Error */
    mpc_err_print(r.error);
    mpc_err_delete(r.error);
    return 1;
  }
  return 0;
}

#if __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int mlisp_init() {
  Number = mpc_new("number");
  Symbol = mpc_new("symbol");
  String = mpc_new("string");
  Comment = mpc_new("comment");
  Sexpr = mpc_new("sexpr");
  Qexpr = mpc_new("qexpr");
  Expr = mpc_new("expr");
  Mlisp = mpc_new("mlisp");

  mpca_lang(MPCA_LANG_DEFAULT, "                  \
    number : /-?[0-9]+/ ;                         \
    symbol : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&|]+/ ;  \
    string : /\"(\\\\.|[^\"])*\"/ ;               \
    comment: /;[^\\r\\n]*/ ;                      \
    sexpr  : '(' <expr>* ')' ;                    \
    qexpr  : '{' <expr>* '}' ;                    \
    expr   : <number> | <symbol> | <string>       \
              | <comment> | <sexpr> | <qexpr> ;   \
    mlisp  : /^/ <expr>* /$/ ;                    \
  ",
            Number, Symbol, String, Comment, Sexpr, Qexpr, Expr, Mlisp);

  lenv *e = lenv_new();
  lenv_add_builtins(e);

  int err;

  if ((err = init_stdlib(e))) {
    printf("Error: Could not initialize stdlib!\n");
    return err;
  }

  globalEnv = e;

  return 0;
}

#if __EMSCRIPTEN__
char *out = NULL;

EMSCRIPTEN_KEEPALIVE
char *mlisp_interpret(char *input) {
  if (out != NULL) {
    free(out);
    out = NULL;
  }
  /* Attempt to Parse the user Input */
  mpc_result_t r;
  if (mpc_parse("<stdin>", input, Mlisp, &r)) {
    lval *x = lval_eval(globalEnv, lval_read(r.output));
    out = lval_to_str(x);
    lval_del(x);
    mpc_ast_delete(r.output);
  } else {
    /* Otherwise Print the Error */
    char *error = mpc_err_string(r.error);
    out = malloc(sizeof(char) * (strlen(error) + 1));
    sprintf(out, "%s", error);
    mpc_err_delete(r.error);
  }
  return out;
}
#endif

#if __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void mlisp_cleanup() {
  lenv_del(globalEnv);

  mpc_cleanup(8, Number, Symbol, String, Comment, Sexpr, Qexpr, Expr, Mlisp);

  #if __EMSCRIPTEN__
  if (out != NULL) {
    free(out);
    out = NULL;
  }
  #endif
}

#if !__EMSCRIPTEN__
void repl() {
  while (1) {
    /* Now in either case readline will be correctly defined */
    char *input = readline("mlisp> ");
    add_history(input);

    /* Attempt to Parse the user Input */
    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Mlisp, &r)) {
      lval *x = lval_eval(globalEnv, lval_read(r.output));
      lval_println(x);
      lval_del(x);
      mpc_ast_delete(r.output);
    } else {
      /* Otherwise Print the Error */
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }

    free(input);
  }
}

int main(int argc, char **argv) {
  int err;
  if ((err = mlisp_init())) {
    return err;
  }

  /* Supplied with list of files */
  if (argc >= 2) {

    /* loop over each supplied filename (starting from 1) */
    for (int i = 1; i < argc; i++) {

      /* Argument list with a single argument, the filename */
      lval *args = lval_add(lval_sexpr(), lval_str(argv[i]));

      /* Pass to builtin load and get the result */
      lval *x = builtin_load(globalEnv, args);

      /* If the result is an error be sure to print it */
      if (x->type == LVAL_ERR) {
        lval_println(x);
      }
      lval_del(x);
    }
  } else {
    puts("mlisp Version 0.0.0.0.1");
    puts("Press Ctrl+c to Exit\n");
    repl();
  }

  mlisp_cleanup();

  return 0;
}
#endif
