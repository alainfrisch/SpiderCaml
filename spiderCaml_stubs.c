#include <string.h>
#include <assert.h>

#define int64 caml_int64
#define int32 caml_int32
#define int16 caml_int16
#define uint64 caml_uint64
#define uint32 caml_uint32
#define uint16 caml_uint16
#include <caml/mlvalues.h>
#include <caml/callback.h>
#include <caml/memory.h>
#include <caml/alloc.h>
#include <caml/custom.h>
#include <caml/fail.h>
#undef int64
#undef int32
#undef int16
#undef uint64
#undef uint32
#undef uint16

#define XP_UNIX
#include "jsapi.h"

#define STACK_CHUNK_SIZE    8192

void fail(char* s){
  printf("%s\n",s);
  exit(2);
}

#define raise_invalid_type() \
  raise_constant(*caml_named_value("js invalid type"))

/* Runtime manipulation */


struct caml_js_rt {
  int ref;   /* Number of objects using this struct */

  value ocaml_objs;    /* Array of rooted OCaml objects.
			  NULL if runtime destroyed explicitly. */
  int ocaml_objs_size;
};

typedef struct caml_js_rt caml_js_rt;

#define rt_info(rt) ((caml_js_rt *) JS_GetRuntimePrivate(rt))
#define get_rt(v) (*((JSRuntime**)Data_custom_val(v)))
#define rt_ocaml_obj(rt,id) (Field(rt_info(rt)->ocaml_objs,id))
#define release_ocaml_obj(rt,id) Store_field(rt_info(rt)->ocaml_objs,id,Val_int(0))
/* TODO: re-use the slot (keep a freelist in ocaml_objs) */

#define check_rt_alive(rt) \
  if (!rt_info(rt)->ocaml_objs) \
   raise_constant(*caml_named_value("js runtime destroyed")); 

#define check_rt_val(r,data) \
  if (data->rt != r) \
   raise_constant(*caml_named_value("js invalid runtime")); 

static void caml_js_rt_dec_ref(JSRuntime *rt) {
  caml_js_rt *data = rt_info(rt);
  if (--data->ref) return;
  if (data->ocaml_objs) { remove_global_root(&data->ocaml_objs); }
  free(data);
  JS_DestroyRuntime(rt);
}

static void caml_js_rt_finalize(value b) {
  caml_js_rt_dec_ref(get_rt(b));
}

static struct custom_operations caml_js_rt_ops = {
  "caml_js_rt",
  caml_js_rt_finalize,
  custom_compare_default,
  custom_hash_default,
  custom_serialize_default,
  custom_deserialize_default
};

static value caml_js_wrap_runtime(JSRuntime *rt) {
  value b = alloc_custom(&caml_js_rt_ops,sizeof(JSRuntime*),1,100);
  get_rt(b) = rt;
  rt_info(rt)->ref++;
  return b;
}

CAMLprim value caml_js_new_runtime(value unit) {
  JSRuntime *rt =  JS_NewRuntime(0x400000L);
  if (!rt)
    fail("can't create JavaScript runtime");

  caml_js_rt* data = malloc(sizeof(caml_js_rt));
  data->ref = 1;
  data->ocaml_objs_size = 0;
  data->ocaml_objs = alloc_tuple(10);
  register_global_root(&data->ocaml_objs);
  JS_SetRuntimePrivate(rt,data);

  value b = alloc_custom(&caml_js_rt_ops,sizeof(JSRuntime*),1,100);
  get_rt(b) = rt;

  return b;
}

static JSRuntime *caml_js_get_rt(value b){
  JSRuntime *rt = get_rt(b);
  check_rt_alive(rt);
  return rt;
}

CAMLprim value caml_js_destroy_runtime(value b){
  JSRuntime *rt = caml_js_get_rt(b);
  caml_js_rt *data = rt_info(rt);
  remove_global_root(&data->ocaml_objs);
  data->ocaml_objs = (value) NULL;
  return Val_unit;
}

static int caml_js_register_ocaml_obj(JSRuntime *rt, value v) {
  CAMLparam1(v);
  CAMLlocal1(slots);
 
  check_rt_alive(rt);
  caml_js_rt *data = rt_info(rt);

  int s = Wosize_val(data->ocaml_objs);
  if (s == data->ocaml_objs_size) {
    int i;
    slots = alloc_tuple(2 * s + 10);
    for (i = 0; i < s; i++) {
      Store_field(slots,i,Field(data->ocaml_objs,i));
    }
    data->ocaml_objs = slots;
  }
  int i = data->ocaml_objs_size++;
  Store_field(data->ocaml_objs,i,v);
  CAMLreturn(i);
}

/* Errors */

struct caml_js_error {
  char *message;
  char *filename;
  char *line;
  int lineno;
  int colno;
};

typedef struct caml_js_error caml_js_error;

static void caml_js_free_error(caml_js_error *e) {
  if (e->message) { free(e->message); }
  if (e->filename) { free(e->filename); }
  if (e->line) { free(e->line); }
}

/* Context manipulation */

struct caml_js_ctx {
  int ref;
  caml_js_error last_error;
};


typedef struct caml_js_ctx caml_js_ctx;

#define ctx_info(rt) ((caml_js_ctx *) JS_GetContextPrivate(rt))
#define get_ctx(v) (*((JSContext**)Data_custom_val(v)))


CAMLprim value caml_js_rt_of_context(value v) {
  return caml_js_wrap_runtime(JS_GetRuntime(get_ctx(v)));
}

static void caml_js_ctx_finalize(value b) {
  JSContext *cx = get_ctx(b);
  caml_js_ctx *data = ctx_info(cx);
  if (--data->ref) return;
  caml_js_free_error(&data->last_error);
  free(data);
  JS_DestroyContext(cx);
  caml_js_rt_dec_ref(JS_GetRuntime(cx));
}

static struct custom_operations caml_js_ctx_ops = {
  "caml_js_ctx",
  caml_js_ctx_finalize,
  custom_compare_default,
  custom_hash_default,
  custom_serialize_default,
  custom_deserialize_default
};

static value caml_js_wrap_context(JSContext *cx) {
  value b = alloc_custom(&caml_js_ctx_ops,sizeof(JSContext*),1,10000);
  get_ctx(b) = cx;
  ctx_info(cx)->ref++;
  return b;
}

static JSBool caml_js_custom_getProperty(JSContext*,JSObject*,jsval,jsval*);
static JSBool caml_js_custom_setProperty(JSContext*,JSObject*,jsval,jsval*);
static void caml_js_custom_finalize(JSContext*,JSObject*);

JSClass caml_js_class = {
  "caml_js_class",JSCLASS_HAS_PRIVATE,
  JS_PropertyStub,JS_PropertyStub,
  caml_js_custom_getProperty,caml_js_custom_setProperty,
  JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub,caml_js_custom_finalize
};


#define copy_str(s) strcpy((char*) malloc(strlen(s)+1),s)
#define opt_copy_str(s) (s?copy_str(s):NULL)

static void caml_js_error_reporter(JSContext *cx, const char *message,
				   JSErrorReport *report) {
  caml_js_ctx *data = ctx_info(cx);
  caml_js_error *err = &data->last_error;
  caml_js_free_error(err);

  err->message = copy_str(message);
  err->filename = opt_copy_str(report->filename);
  err->line = opt_copy_str(report->linebuf);
  err->lineno = report->lineno;
  err->colno = (report->linebuf?report->tokenptr - report->linebuf:0);
}

CAMLprim value caml_js_new_context(value rtv,value ops) {
  CAMLparam2(rtv,ops);
  CAMLlocal1(b);

  JSRuntime *rt = caml_js_get_rt(rtv);

  JSContext *cx = JS_NewContext(rt, STACK_CHUNK_SIZE);
  if (!cx)
    fail("can't create JavaScript context");

  rt_info(rt)->ref++;

  JSObject *obj = JS_NewObject(cx, &caml_js_class, 0, 0);
  int slot = (Is_block(ops)?caml_js_register_ocaml_obj(rt,Field(ops,0)):(-1));
  JS_SetPrivate(cx,obj,(void*) (slot << 1));
  JS_InitStandardClasses(cx, obj);

  b = alloc_custom(&caml_js_ctx_ops,sizeof(JSContext*),1,10000);
  get_ctx(b) = cx;

  caml_js_ctx *ctx = malloc(sizeof(caml_js_ctx));
  JS_SetContextPrivate(cx, ctx);
  ctx->ref = 1;
  ctx->last_error.message = NULL;
  ctx->last_error.filename = NULL;
  ctx->last_error.line = NULL;

  JS_SetErrorReporter(cx, caml_js_error_reporter);

  CAMLreturn(b);
}

static value make_some(value v) {
  CAMLparam1(v);
  CAMLlocal1(b);
  b = alloc_tuple(1);
  Store_field(b,0,v);
  CAMLreturn(b);
}

#define opt_str(s) (s?make_some(copy_string(s)):Val_int(0))

static value caml_js_raise_ctx_error(JSContext *cx) {
  CAMLparam0();
  CAMLlocal1(b);

  caml_js_ctx *data = ctx_info(cx);
  assert(data->last_error.message);

  b = alloc_tuple(5);
  Store_field(b,0,copy_string(data->last_error.message));
  Store_field(b,1,opt_str(data->last_error.filename));
  Store_field(b,2,opt_str(data->last_error.line));
  Store_field(b,3,Val_int(data->last_error.lineno));
  Store_field(b,4,Val_int(data->last_error.colno));
  raise_with_arg(*caml_named_value("js eval error"), b); 
  CAMLreturn(Val_unit);
}

/* JS values */

struct caml_js_val {
  jsval v;
  JSRuntime *rt;
};

typedef struct caml_js_val caml_js_val;

#define get_val(v) (*((caml_js_val**)Data_custom_val(v)))

CAMLprim value caml_js_rt_of_value(value v) {
  return caml_js_wrap_runtime(get_val(v)->rt);
}

static value caml_js_false = 0;
static value caml_js_true = 4;
static value caml_js_null = 8;
static value caml_js_void = 12;

static void caml_js_val_finalize(value b) {
  caml_js_val *data = get_val(b);

  JS_RemoveRootRT(data->rt,&data->v);
  caml_js_rt_dec_ref(data->rt);
  free(data);
}

static struct custom_operations caml_js_val_ops = {
  "caml_js_val",
  caml_js_val_finalize,
  custom_compare_default,
  custom_hash_default,
  custom_serialize_default,
  custom_deserialize_default
};
  
static value caml_js_create_val(JSRuntime *rt,jsval f) {
  CAMLparam0();
  CAMLlocal1(b);

  caml_js_val *data = malloc(sizeof(caml_js_val));
  b = alloc_custom(&caml_js_val_ops,sizeof(caml_js_val*),1,50000);
  get_val(b) = data;
  data->rt = rt;
  data->v = f;
  JS_AddNamedRootRT(rt,&data->v,"root");
  rt_info(rt)->ref++;
  CAMLreturn(b);
}


static value jsval_to_caml(JSRuntime *rt, jsval v){
  if (JSVAL_IS_INT(v)) { return v; }
  if (v == JSVAL_FALSE) { return caml_js_false; }
  if (v == JSVAL_TRUE) { return caml_js_true; }
  if (v == JSVAL_NULL) { return caml_js_null; }
  if (v == JSVAL_VOID) { return caml_js_void; }
  return caml_js_create_val(rt,v);
}

static jsval caml_to_jsval(JSRuntime *rt, value v){
  if (Is_long(v)) { return (jsval)(v); }
  if (v == caml_js_true) { return JSVAL_TRUE; }
  if (v == caml_js_false) { return JSVAL_FALSE; }
  if (v == caml_js_null) { return JSVAL_NULL; }
  if (v == caml_js_void) { return JSVAL_VOID; }

  caml_js_val *data = get_val(v);
  check_rt_val(rt,data);
  return data->v;
}



#define jsstring_to_caml(str) copy_string(JS_GetStringBytes(str))

CAMLprim value caml_js_to_string(value ctx, value v){
  CAMLparam2(ctx,v);

  if (Is_long(v)) { CAMLreturn(copy_string("<int>")); }
  if (v == caml_js_true) {  CAMLreturn(copy_string("true")); }
  if (v == caml_js_false) {  CAMLreturn(copy_string("false")); }
  if (v == caml_js_null) {  CAMLreturn(copy_string("null")); }
  if (v == caml_js_void) {  CAMLreturn(copy_string("undefined")); }

  caml_js_val *data = get_val(v);
  JSContext *cx = get_ctx(ctx);
  check_rt_val(JS_GetRuntime(cx),data);
  CAMLreturn(jsstring_to_caml(JS_ValueToString(cx,data->v)));
}



/* Value constructors */

static JSBool caml_trampoline(JSContext *cx, JSObject *obj, 
		   uintN argc, jsval *argv, jsval *rval)
{
  CAMLparam0();
  CAMLlocal1(args);
  jsval slot;

  args = alloc(argc,0);
  JSRuntime *rt = JS_GetRuntime(cx);
  check_rt_alive(rt);
  int i;
  for (i = 0; i < argc; i++) {
    Store_field(args,i,jsval_to_caml(rt,argv[i]));
  }

  JS_GetReservedSlot(cx,(JSObject*)argv[-2],0,&slot);
  int func_id = JSVAL_TO_INT(slot);
  value res = 
    callback2(rt_ocaml_obj(rt,func_id),caml_js_wrap_context(cx),args);
  *rval = caml_to_jsval(rt, res);
  CAMLreturn(JS_TRUE);
}


CAMLprim value caml_js_special_csts(value v){
  switch Int_val(v) {
  case 0: return caml_js_false;
  case 1: return caml_js_true;
  case 2: return caml_js_null;
  case 3: return caml_js_void;
  }
  fail("caml_js_special_csts");
  exit(1);
}

CAMLprim value caml_js_new_string(value cx, value v){
  JSContext *ctx = get_ctx(cx);
  return jsval_to_caml(JS_GetRuntime(ctx),
		       STRING_TO_JSVAL(JS_InternString(ctx,String_val(v))));
}

CAMLprim value caml_js_new_double(value cx, value v){
  JSContext *ctx = get_ctx(cx);
  return jsval_to_caml(JS_GetRuntime(ctx),
		       DOUBLE_TO_JSVAL(JS_NewDouble(ctx,Double_val(v))));
}

CAMLprim value caml_js_new_function(value cx, value name, value f){
  CAMLparam3(cx,f,name);
  JSContext *ctx = get_ctx(cx);
  JSRuntime *rt = JS_GetRuntime(ctx);
  JSFunction *func =
    JS_NewFunction(ctx, caml_trampoline, 0, 0, NULL, String_val(name));
  if (!func)
    fail("Cannot create JS function");

  int slot = caml_js_register_ocaml_obj(rt,f);
  JSObject *obj = JS_GetFunctionObject(func);
  JS_SetReservedSlot(ctx,obj,0,INT_TO_JSVAL(slot));
  CAMLreturn(jsval_to_caml(rt,OBJECT_TO_JSVAL(obj)));
}

CAMLprim value caml_js_to_bool(value ctx, value v){
  CAMLparam2(ctx,v);
  JSContext *cx = get_ctx(ctx);
  JSRuntime *rt = JS_GetRuntime(cx);
  jsval jv = caml_to_jsval(rt,v);
  JSBool o;
  JSBool ok = JS_ValueToBoolean(cx,jv,&o);
  if (!ok) { raise_invalid_type(); }
  CAMLreturn(Val_bool(o));
}

CAMLprim value caml_js_to_number(value ctx, value v){
  CAMLparam2(ctx,v);
  JSContext *cx = get_ctx(ctx);
  JSRuntime *rt = JS_GetRuntime(cx);
  jsval jv = caml_to_jsval(rt,v);
  jsdouble o;
  JSBool ok = JS_ValueToNumber(cx,jv,&o);
  if (!ok) { raise_invalid_type(); }
  CAMLreturn(copy_double(o));
}

/* Objects */

#define define_tester(funname,pred)                                  \
  CAMLprim value funname(value ctx, value v){                        \
    return Val_bool(pred(caml_to_jsval(JS_GetRuntime(get_ctx(ctx)),v))); \
  }



#define tester_getter(funname,pred,unw,wr)                                  \
  CAMLprim value caml_js_is_##funname(value cx, value v){                   \
    return Val_bool(pred(caml_to_jsval(JS_GetRuntime(get_ctx(cx)),v)));  \
  }                                                                         \
  CAMLprim value caml_js_get_##funname(value cx, value v){                  \
    CAMLparam2(cx,v);							    \
    JSContext *ctx = get_ctx(cx);					    \
    JSRuntime *rt = JS_GetRuntime(ctx);					    \
    jsval jv = caml_to_jsval(rt,v);					    \
    if (pred(jv)) { CAMLreturn(wr(unw(jv))); }				    \
    else { raise_invalid_type(); }                                         \
  }

#define jsdouble_to_caml(o) copy_double(*o)

define_tester(caml_js_is_object,JSVAL_IS_OBJECT);
define_tester(caml_js_is_number,JSVAL_IS_NUMBER);
define_tester(caml_js_is_null,JSVAL_IS_NULL);
define_tester(caml_js_is_void,JSVAL_IS_VOID);

tester_getter(boolean,JSVAL_IS_BOOLEAN,JSVAL_TO_BOOLEAN,Val_bool);
tester_getter(int,JSVAL_IS_INT,JSVAL_TO_INT,Val_int);
tester_getter(string,JSVAL_IS_STRING,JSVAL_TO_STRING,jsstring_to_caml);
tester_getter(double,JSVAL_IS_DOUBLE,JSVAL_TO_DOUBLE,jsdouble_to_caml);

#define wrap_obj(rt,o) jsval_to_caml(rt,OBJECT_TO_JSVAL(o))

CAMLprim value caml_js_to_object(value ctx, value v){
  CAMLparam2(ctx,v);
  JSContext *cx = get_ctx(ctx);
  JSRuntime *rt = JS_GetRuntime(cx);
  jsval jv = caml_to_jsval(rt,v);
  JSObject *o;
  JSBool ok = JS_ValueToObject(cx,jv,&o);
  if (!ok) { raise_invalid_type(); }
  CAMLreturn(wrap_obj(rt,o));
}

static JSObject* unwrap_obj(JSRuntime *rt, value v){
  jsval jv = caml_to_jsval(rt,v);
  if (JSVAL_IS_OBJECT(jv)) { return JSVAL_TO_OBJECT(jv); }
  else { raise_constant(*caml_named_value("js invalid type")); }  
}

CAMLprim value caml_js_get_global_object(value cx) {
  CAMLparam1(cx);
  JSContext *ctx = get_ctx(cx);

  CAMLreturn(wrap_obj(JS_GetRuntime(ctx), JS_GetGlobalObject(ctx)));
}

CAMLprim value caml_js_set_prop(value cx, value obj, value name, value v){
  CAMLparam4(cx,name,obj,v);
  JSContext *ctx = get_ctx(cx);
  JSRuntime *rt = JS_GetRuntime(ctx);
  jsval jv = caml_to_jsval(rt,v); 
  JSBool ok = JS_SetProperty(ctx,
		 unwrap_obj(rt,obj),
		 String_val(name),
		 &jv);
  if (!ok)
    failwith("js set property");
  CAMLreturn(Val_unit);
}

CAMLprim value caml_js_get_prop(value cx, value obj, value name){
  CAMLparam3(cx,name,obj);
  JSContext *ctx = get_ctx(cx);
  JSRuntime *rt = JS_GetRuntime(ctx);
  jsval ret;
  JSBool ok = JS_GetProperty(ctx,
		 unwrap_obj(rt,obj),
		 String_val(name),
		 &ret);
  if (!ok)
    failwith("js get property");
  CAMLreturn(jsval_to_caml(rt,ret));
}
CAMLprim value caml_js_set_elem(value cx, value obj, value idx, value v){
  CAMLparam4(cx,idx,obj,v);
  JSContext *ctx = get_ctx(cx);
  JSRuntime *rt = JS_GetRuntime(ctx);
  jsval jv = caml_to_jsval(rt,v); 
  JSBool ok = JS_SetElement(ctx,
		 unwrap_obj(rt,obj),
		 Int_val(idx),
		 &jv);
  if (!ok)
    failwith("js set elem");
  CAMLreturn(Val_unit);
}

CAMLprim value caml_js_get_elem(value cx, value obj, value idx){
  CAMLparam3(cx,idx,obj);
  JSContext *ctx = get_ctx(cx);
  JSRuntime *rt = JS_GetRuntime(ctx);
  jsval ret;
  JSBool ok = JS_GetElement(ctx,
		 unwrap_obj(rt,obj),
		 Int_val(idx),
		 &ret);
  if (!ok)
    failwith("js get elem");
  CAMLreturn(jsval_to_caml(rt,ret));
}

CAMLprim value caml_js_enumerate(value cx, value obj){
  CAMLparam2(cx,obj);
  CAMLlocal2(res,elt);
  JSContext *ctx = get_ctx(cx);
  JSRuntime *rt = JS_GetRuntime(ctx);
  JSIdArray *props = JS_Enumerate(ctx, unwrap_obj(rt,obj));
  jsval val;
  jsid *ptr, *head;
  res = Val_int(0);
  head = ptr = props->vector;
  ptr += (props->length - 1);
  while (ptr >= head) {
    if (JS_IdToValue(ctx,*ptr,&val)) {
      elt = res;
      res = caml_alloc(2,0);
      Store_field(res, 0, jsval_to_caml(rt,val));
      Store_field(res, 1, elt);
    }
    ptr--;
  }
  JS_DestroyIdArray(ctx, props);
  CAMLreturn(res);
}


CAMLprim value caml_js_new_object(value cx, value proto, value parent, value ops){
  CAMLparam4(cx,proto,parent,ops);
  JSContext *ctx = get_ctx(cx);
  JSRuntime *rt = JS_GetRuntime(ctx);
  JSObject *oproto = (Is_long(proto)?NULL:unwrap_obj(rt,Field(proto,0)));
  JSObject *oparent = (Is_long(parent)?NULL:unwrap_obj(rt,Field(parent,0)));
  JSObject *obj = JS_NewObject(ctx,&caml_js_class,oproto,oparent);
  int slot = (Is_block(ops)?caml_js_register_ocaml_obj(rt,Field(ops,0)):(-1));
  JS_SetPrivate(ctx,obj,(void*) (slot << 1));
  CAMLreturn(wrap_obj(rt,obj));
}



CAMLprim value caml_js_evaluate_script(value cx, value obj, value script){
  CAMLparam2(cx,script);
  JSContext *ctx = get_ctx(cx);
  JSRuntime *rt = JS_GetRuntime(ctx);
  jsval rval; 
  JSBool ok;
  ok = JS_EvaluateScript(ctx, unwrap_obj(rt,obj), String_val(script), 
			 string_length(script),
			 NULL, 0, &rval);
  if (!ok)
    caml_js_raise_ctx_error(ctx);
  CAMLreturn(jsval_to_caml(JS_GetRuntime(ctx),rval));
}


/* Arrays */

CAMLprim value caml_js_new_array(value cx){
  JSContext *ctx = get_ctx(cx);
  return wrap_obj(JS_GetRuntime(ctx),JS_NewArrayObject(ctx,0, NULL));
}

CAMLprim value caml_js_is_array(value cx, value v){
  JSContext *ctx = get_ctx(cx);
  jsval jv = caml_to_jsval(JS_GetRuntime(ctx),v);
  return 
    Val_bool(JSVAL_IS_OBJECT(jv) && JS_IsArrayObject(ctx,JSVAL_TO_OBJECT(jv)));
}

/* Misc */


CAMLprim value caml_js_implementation_version(value unit){
  return copy_string(JS_GetImplementationVersion());
}

CAMLprim value caml_js_get_version(value cx){
  return Val_int (JS_GetVersion(get_ctx(cx)));
}

CAMLprim value caml_js_set_version(value cx, value v){
  JS_SetVersion(get_ctx(cx),Int_val(v));
  return Val_int(0);
}



#define getset_prop(funname,meth) \
static JSBool funname			\
(JSContext *cx, JSObject *obj, jsval id, jsval *vp){		\
  CAMLparam0();							\
  CAMLlocal5(o,prop_name,prop_val,ctx,res);			\
  JSRuntime *rt = JS_GetRuntime(cx);				\
  int slot = ((int) JS_GetPrivate(cx,obj)) >> 1;		\
  if (slot != (-1)) {						\
    o = Field(rt_ocaml_obj(rt,slot),meth);			\
    prop_name = jsstring_to_caml(JS_ValueToString(cx, id));	\
    prop_val = jsval_to_caml(rt,*vp);				\
    ctx = caml_js_wrap_context(cx);				\
    res = callback3(o,ctx,prop_name,prop_val);			\
    *vp = caml_to_jsval(rt,res);				\
  }								\
  CAMLreturn(JS_TRUE);						\
}

getset_prop(caml_js_custom_getProperty,0);
getset_prop(caml_js_custom_setProperty,1);


static void caml_js_custom_finalize(JSContext *cx, JSObject *obj){
  JSRuntime *rt = JS_GetRuntime(cx);			
  int slot = ((int) JS_GetPrivate(cx,obj)) >> 1;      	
  if (slot != (-1)) {	
    release_ocaml_obj(rt,slot);
  }
}
