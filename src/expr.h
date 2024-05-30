/***************************************************************/
/*                                                             */
/*  EXPR_NEW.H                                                 */
/*                                                             */
/*  Contains a few definitions used by expression pareser and  */
/*  evaluator.                                                 */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 2022 by Dianne Skoll                         */
/*                                                             */
/***************************************************************/

typedef struct udf_struct UserFunc;

expr_node *parse_expression(char const **e, int *r, Var *locals);

/* Define the types of values */
#define ERR_TYPE       0
#define INT_TYPE       1
#define TIME_TYPE      2
#define DATE_TYPE      3
#define STR_TYPE       4
#define DATETIME_TYPE  5
#define SPECIAL_TYPE   6 /* Only for system variables */
#define CONST_INT_TYPE 7 /* Only for system variables */

#define BEG_OF_EXPR '['
#define END_OF_EXPR ']'
#define COMMA ','

/* These functions are in utils.c and are used to detect overflow
   in various arithmetic operators.  They have to be in separate
   functions with extern linkage to defeat compiler optimizations
   that would otherwise break the overflow checks. */
extern int _private_mul_overflow(int a, int b);
extern int _private_add_overflow(int a, int b);
extern int _private_sub_overflow(int a, int b);
