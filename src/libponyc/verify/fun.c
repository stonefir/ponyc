#include "fun.h"
#include "../type/subtype.h"
#include <string.h>
#include <assert.h>


static bool verify_main_create(pass_opt_t* opt, ast_t* ast)
{
  if(ast_id(opt->check.frame->type) != TK_ACTOR)
    return true;

  ast_t* type_id = ast_child(opt->check.frame->type);

  if(strcmp(ast_name(type_id), "Main"))
    return true;

  AST_GET_CHILDREN(ast, cap, id, typeparams, params, result, can_error);
  ast_t* type = ast_parent(ast_parent(ast));

  if(strcmp(ast_name(id), "create"))
    return true;

  bool ok = true;

  if(ast_id(ast) != TK_NEW)
  {
    ast_error(opt->check.errors, ast,
      "the create method of the Main actor must be a constructor");
    ok = false;
  }

  if(ast_id(typeparams) != TK_NONE)
  {
    ast_error(opt->check.errors, typeparams,
      "the create constructor of the Main actor must not take type parameters");
    ok = false;
  }

  if(ast_childcount(params) != 1)
  {
    if(ast_pos(params) == ast_pos(type))
      ast_error(opt->check.errors, params,
        "The Main actor must have a create constructor which takes only a "
        "single Env parameter");
    else
      ast_error(opt->check.errors, params,
        "the create constructor of the Main actor must take only a single Env"
        "parameter");
    ok = false;
  }

  ast_t* param = ast_child(params);

  if(param != NULL)
  {
    ast_t* p_type = ast_childidx(param, 1);

    if(!is_env(p_type))
    {
      ast_error(opt->check.errors, p_type, "must be of type Env");
      ok = false;
    }
  }

  return ok;
}

static bool verify_primitive_init(pass_opt_t* opt, ast_t* ast)
{
  if(ast_id(opt->check.frame->type) != TK_PRIMITIVE)
    return true;

  AST_GET_CHILDREN(ast, cap, id, typeparams, params, result, can_error);

  if(strcmp(ast_name(id), "_init"))
    return true;

  bool ok = true;

  if(ast_id(ast_childidx(opt->check.frame->type, 1)) != TK_NONE)
  {
    ast_error(opt->check.errors, ast,
      "a primitive with type parameters cannot have an _init method");
    ok = false;
  }

  if(ast_id(ast) != TK_FUN)
  {
    ast_error(opt->check.errors, ast,
      "a primitive _init method must be a function");
    ok = false;
  }

  if(ast_id(cap) != TK_BOX)
  {
    ast_error(opt->check.errors, cap,
      "a primitive _init method must use box as the receiver capability");
    ok = false;
  }

  if(ast_id(typeparams) != TK_NONE)
  {
    ast_error(opt->check.errors, typeparams,
      "a primitive _init method must not take type parameters");
    ok = false;
  }

  if(ast_childcount(params) != 0)
  {
    ast_error(opt->check.errors, params,
      "a primitive _init method must take no parameters");
    ok = false;
  }

  if(!is_none(result))
  {
    ast_error(opt->check.errors, result,
      "a primitive _init method must return None");
    ok = false;
  }

  if(ast_id(can_error) != TK_NONE)
  {
    ast_error(opt->check.errors, can_error,
      "a primitive _init method cannot be a partial function");
    ok = false;
  }

  return ok;
}

static bool verify_any_final(pass_opt_t* opt, ast_t* ast)
{
  AST_GET_CHILDREN(ast, cap, id, typeparams, params, result, can_error, body);

  if(strcmp(ast_name(id), "_final"))
    return true;

  bool ok = true;

  if((ast_id(opt->check.frame->type) == TK_PRIMITIVE) &&
    (ast_id(ast_childidx(opt->check.frame->type, 1)) != TK_NONE))
  {
    ast_error(opt->check.errors, ast,
      "a primitive with type parameters cannot have a _final method");
    ok = false;
  }

  if(ast_id(ast) != TK_FUN)
  {
    ast_error(opt->check.errors, ast, "a _final method must be a function");
    ok = false;
  }

  if(ast_id(cap) != TK_BOX)
  {
    ast_error(opt->check.errors, cap,
      "a _final method must use box as the receiver capability");
    ok = false;
  }

  if(ast_id(typeparams) != TK_NONE)
  {
    ast_error(opt->check.errors, typeparams,
      "a _final method must not take type parameters");
    ok = false;
  }

  if(ast_childcount(params) != 0)
  {
    ast_error(opt->check.errors, params,
      "a _final method must take no parameters");
    ok = false;
  }

  if(!is_none(result))
  {
    ast_error(opt->check.errors, result, "a _final method must return None");
    ok = false;
  }

  if(ast_id(can_error) != TK_NONE)
  {
    ast_error(opt->check.errors, can_error,
      "a _final method cannot be a partial function");
    ok = false;
  }

  return ok;
}

static bool show_partiality(pass_opt_t* opt, ast_t* ast)
{
  ast_t* child = ast_child(ast);
  bool found = false;

  while(child != NULL)
  {
    if(ast_canerror(child))
      found |= show_partiality(opt, child);

    child = ast_sibling(child);
  }

  if(found)
    return true;

  if(ast_canerror(ast))
  {
    ast_error_continue(opt->check.errors, ast, "an error can be raised here");
    return true;
  }

  return false;
}

bool verify_fun(pass_opt_t* opt, ast_t* ast)
{
  assert((ast_id(ast) == TK_BE) || (ast_id(ast) == TK_FUN) ||
    (ast_id(ast) == TK_NEW));
  AST_GET_CHILDREN(ast, cap, id, typeparams, params, type, can_error, body);

  // Run checks tailored to specific kinds of methods, if any apply.
  if(!verify_main_create(opt, ast) ||
    !verify_primitive_init(opt, ast) ||
    !verify_any_final(opt, ast))
    return false;

  // Check partial functions.
  if(ast_id(can_error) == TK_QUESTION)
  {
    // If the function is marked as partial, it must have the potential
    // to raise an error somewhere in the body. This check is skipped for
    // traits and interfaces - they are allowed to give a default implementation
    // of the method that does or does not have the potential to raise an error.
    bool is_trait =
      (ast_id(opt->check.frame->type) == TK_TRAIT) ||
      (ast_id(opt->check.frame->type) == TK_INTERFACE) ||
      (ast_id((ast_t*)ast_data(ast)) == TK_TRAIT) ||
      (ast_id((ast_t*)ast_data(ast)) == TK_INTERFACE);

    if(!is_trait &&
      !ast_canerror(body) &&
      (ast_id(ast_type(body)) != TK_COMPILE_INTRINSIC))
    {
      ast_error(opt->check.errors, can_error, "function signature is marked as "
        "partial but the function body cannot raise an error");
      return false;
    }
  } else {
    // If the function is not marked as partial, it must never raise an error.
    if(ast_canerror(body))
    {
      ast_error(opt->check.errors, can_error, "function signature is not "
        "marked as partial but the function body can raise an error");
      show_partiality(opt, body);
      return false;
    }
  }

  return true;
}
