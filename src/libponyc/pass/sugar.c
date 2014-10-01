#include "sugar.h"
#include "../ast/token.h"
#include "../pkg/package.h"
#include "../type/assemble.h"
#include "../ds/stringtab.h"
#include <assert.h>


static ast_t* make_create(ast_t* ast)
{
  BUILD(create, ast,
    NODE(TK_NEW,
      NONE          // cap
      ID("create")  // name
      NONE          // typeparams
      NONE          // params
      NONE          // return type
      NONE          // error
      NODE(TK_SEQ, INT(0))));

  return create;
}


static bool add_default_constructor(ast_t* ast, ast_t* members)
{
  ast_t* member = ast_child(members);
  const char* create = stringtab("create");
  ast_t* create_member = NULL;

  // If we have no fields and have no "create" constructor, add one
  while(member != NULL)
  {
    switch(ast_id(member))
    {
      case TK_FVAR:
      case TK_FLET:
      {
        ast_t* init = ast_childidx(member, 2);

        if(ast_id(init) == TK_NONE)
          return true;

        break;
      }

      case TK_NEW:
      {
        ast_t* id = ast_childidx(member, 1);

        if(ast_id(id) == TK_NONE || ast_name(id) == create)
          return true;

        break;
      }

      case TK_FUN:
      case TK_BE:
      {
        ast_t* id = ast_childidx(member, 1);

        if(create_member == NULL && ast_name(id) == create)
          create_member = member;

        break;
      }

      default: {}
    }

    member = ast_sibling(member);
  }

  if(create_member != NULL)
  {
    // TODO(andy): Should a fun or be called create prevent creation rather
    // than generating an error?
    ast_error(create_member,
      "member create clashes with autogenerated constructor");

    return false;
  }

  ast_add(members, make_create(ast));
  return true;
}


static ast_result_t sugar_member(ast_t* ast, bool add_create,
  token_id def_def_cap)
{
  AST_GET_CHILDREN(ast, ignore0, ignore1, defcap, traits, members);

  if(add_create && !add_default_constructor(ast, members))
    return AST_ERROR;

  if(ast_id(defcap) == TK_NONE)
    ast_setid(defcap, def_def_cap);

  return AST_OK;
}


static ast_result_t sugar_typeparam(ast_t* ast)
{
  ast_t* constraint = ast_childidx(ast, 1);

  // If no constraint is specified for a formal parameter use Any
  // TODO(andy): Should we create a nominal to Any instead of the structural?
  if(ast_id(constraint) == TK_NONE)
  {
    REPLACE(&constraint,
      NODE(TK_STRUCTURAL,
        NODE(TK_MEMBERS)
        NODE(TK_TAG)  // Capability
        NONE));       // Ephemeral
  }

  return AST_OK;
}


static ast_result_t sugar_new(ast_t* ast)
{
  AST_GET_CHILDREN(ast, ignore0, id, ignore2, ignore3, result);
  ast_t* def = ast_enclosing_type(ast);

  if(ast_id(id) == TK_NONE)
  {
    // Set the name to "create" if there isn't one
    ast_replace(&id, ast_from_string(id, "create"));
  }

  // Return type is This ref^ for classes, This val^ for primitives, and
  // This tag^ for actors.
  assert(ast_id(result) == TK_NONE);

  token_id cap = TK_REF;

  if((ast_id(def) == TK_PRIMITIVE) || (ast_id(def) == TK_ACTOR))
    cap = TK_TAG;

  ast_replace(&result, type_for_this(ast, cap, true));
  return AST_OK;
}


static ast_result_t sugar_be(ast_t* ast)
{
  // Return type is This tag
  ast_t* result = ast_childidx(ast, 4);
  assert(ast_id(result) == TK_NONE);

  ast_replace(&result, type_for_this(ast, TK_TAG, false));
  return AST_OK;
}


static ast_result_t sugar_fun(ast_t* ast)
{
  AST_GET_CHILDREN(ast, ignore0, id, ignore2, ignore3, result, ignore5, body);

  // Set the name to "apply" if there isn't one
  if(ast_id(id) == TK_NONE)
    ast_replace(&id, ast_from_string(id, "apply"));

  if(ast_id(result) != TK_NONE)
    return AST_OK;

  // Return value is not specified, set it to None
  ast_t* type = type_sugar(ast, NULL, "None");
  ast_replace(&result, type);

  // Add None at the end of the body, if there is one
  if(ast_id(body) == TK_SEQ)
  {
    ast_t* last = ast_childlast(body);
    BUILD(ref, last, NODE(TK_REFERENCE, ID("None")));
    ast_append(body, ref);
  }

  return AST_OK;
}


static ast_result_t sugar_nominal(ast_t* ast)
{
  // if we didn't have a package, the first two children will be ID NONE
  // change them to NONE ID so the package is always first
  ast_t* package = ast_child(ast);
  ast_t* type = ast_sibling(package);
  if(ast_id(type) == TK_NONE)
  {
    ast_pop(ast);
    ast_pop(ast);
    ast_add(ast, package);
    ast_add(ast, type);
  }
  return AST_OK;
}


static ast_result_t sugar_structural(ast_t* ast)
{
  ast_t* cap = ast_childidx(ast, 1);

  if(ast_id(cap) == TK_NONE)
  {
    token_id defcap;

    // For typeparams default capability is tag, otherwise it is ref
    // TODO(andy): This is not right. The constraint type (if any) of a type
    // parameter defaults to tag since it is an upper bound. The default type
    // (if any) for the type parameter, and any inner structural types of the
    // constraint, should be ref as normal.
    if(ast_nearest(ast, TK_TYPEPARAM) != NULL)
      defcap = TK_TAG;
    else
      defcap = TK_REF;

    ast_setid(cap, defcap);
  }

  return AST_OK;
}


// If the given tree is a TK_NONE expand it to a source None
static void expand_none(ast_t* ast)
{
  if(ast_id(ast) != TK_NONE)
    return;

  ast_setid(ast, TK_SEQ);
  BUILD(ref, ast, NODE(TK_REFERENCE, ID("None")));
  ast_add(ast, ref);
}


static ast_result_t sugar_else(ast_t* ast)
{
  ast_t* else_clause = ast_childidx(ast, 2);
  expand_none(else_clause);
  return AST_OK;
}


static ast_result_t sugar_try(ast_t* ast)
{
  AST_GET_CHILDREN(ast, ignore, else_clause, then_clause);

  expand_none(else_clause);
  expand_none(then_clause);

  return AST_OK;
}


static ast_result_t sugar_for(ast_t** astp)
{
  AST_EXTRACT_CHILDREN(*astp, for_idseq, for_type, for_iter, for_body, for_else);

  expand_none(for_else);
  const char* iter_name = package_hygienic_id_string(*astp);

  REPLACE(astp,
    NODE(TK_SEQ, AST_SCOPE
      NODE(TK_ASSIGN,
        NODE(TK_VAR, NODE(TK_IDSEQ, ID(iter_name)) TREE(for_type))
        TREE(for_iter))
      NODE(TK_WHILE, AST_SCOPE
        NODE(TK_CALL,
          NODE(TK_DOT, NODE(TK_REFERENCE, ID(iter_name)) ID("has_next"))
          NONE NONE)
        NODE(TK_SEQ, AST_SCOPE
          NODE(TK_ASSIGN,
            NODE(TK_VAR, TREE(for_idseq) TREE(for_type))
            NODE(TK_CALL,
              NODE(TK_DOT, NODE(TK_REFERENCE, ID(iter_name)) ID("next"))
              NONE NONE))
          TREE(for_body))
        TREE(for_else))));

  return AST_OK;
}


static ast_result_t sugar_bang(ast_t** astp)
{
  // TODO: syntactic sugar for partial application
  /*
  a!method(b, c)

  {
    var $0: Receiver method_cap = a
    var $1: Param1 = b
    var $2: Param2 = c

    fun cap apply(remaining args on method): method_result =>
      $0.method($1, $2, remaining args on method)
  } cap ^

  cap
    never tag (need to read our receiver)
    never iso or trn (but can recover)
    val: ParamX val or tag, method_cap val or tag
    box: val <: ParamX, val <: method_cap
    ref: otherwise
  */
  return AST_OK;
}


static ast_result_t sugar_case(ast_t* ast)
{
  ast_t* body = ast_childidx(ast, 2);

  if(ast_id(body) != TK_NONE)
    return AST_OK;

  // We have no body, take a copy of the next case with a body
  ast_t* next = ast;
  ast_t* next_body = body;

  while(ast_id(next_body) == TK_NONE)
  {
    next = ast_sibling(next);
    assert(next != NULL);
    assert(ast_id(next) == TK_CASE);
    next_body = ast_childidx(next, 2);
  }

  ast_replace(&body, next_body);
  return AST_OK;
}


static ast_result_t sugar_update(ast_t** astp)
{
  ast_t* ast = *astp;
  assert(ast_id(ast) == TK_ASSIGN);

  AST_GET_CHILDREN(ast, call, value);

  if(ast_id(call) != TK_CALL)
    return AST_OK;

  // We are of the form:  x(y) = z
  // Replace us with:     x.update(y, z)

  // TODO(andy): Due to complications with optional arguments we should hand z
  // in as a named argument, once we have those
  AST_EXTRACT_CHILDREN(call, expr, positional, named);

  // If there are no positional arguments yet, positional will be a TK_NONE
  ast_setid(positional, TK_POSITIONALARGS);
  ast_append(positional, value);

  REPLACE(astp,
    NODE(TK_CALL,
      NODE(TK_DOT, TREE(expr) ID("update"))
      TREE(positional)
      TREE(named)));

  return AST_OK;
}


ast_result_t pass_sugar(ast_t** astp, pass_opt_t* options)
{
  ast_t* ast = *astp;
  assert(ast != NULL);

  switch(ast_id(ast))
  {
    case TK_PRIMITIVE:  return sugar_member(ast, true, TK_VAL);
    case TK_CLASS:      return sugar_member(ast, true, TK_REF);
    case TK_ACTOR:      return sugar_member(ast, true, TK_TAG);
    case TK_TRAIT:      return sugar_member(ast, false, TK_REF);
    case TK_TYPEPARAM:  return sugar_typeparam(ast);
    case TK_NEW:        return sugar_new(ast);
    case TK_BE:         return sugar_be(ast);
    case TK_FUN:        return sugar_fun(ast);
    case TK_NOMINAL:    return sugar_nominal(ast);
    case TK_STRUCTURAL: return sugar_structural(ast);
    case TK_IF:
    case TK_MATCH:
    case TK_WHILE:
    case TK_REPEAT:     return sugar_else(ast);
    case TK_TRY:        return sugar_try(ast);
    case TK_FOR:        return sugar_for(astp);
    case TK_BANG:       return sugar_bang(astp);
    case TK_CASE:       return sugar_case(ast);
    case TK_ASSIGN:     return sugar_update(astp);
    default:            return AST_OK;
  }
}
