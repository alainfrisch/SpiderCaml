module Error = struct

  type t = { message: string;
	     filename: string option;
	     line: string option;
	     lineno: int;
	     colno: int }

  exception RuntimeDestroyed
  exception InvalidRuntime
  exception InvalidType
  exception Message of t

  let _ = Callback.register_exception "js invalid type" InvalidType
  let _ = Callback.register_exception "js runtime destroyed" RuntimeDestroyed
  let _ = Callback.register_exception "js invalid runtime" InvalidRuntime
  let _ = Callback.register_exception "js eval error" (Message (Obj.magic 0))
end

type jsval
type jsctx

type objop = jsctx -> string -> jsval -> jsval
type objops = objop * objop

module Rt = struct
  type t
  external create: unit -> t = "caml_js_new_runtime"
  external destroy: t -> unit = "caml_js_destroy_runtime"
end

module Ctx = struct
  type t = jsctx
  external create: Rt.t -> objops option -> t = "caml_js_new_context"
  external runtime: t -> Rt.t = "caml_js_rt_of_context"
  external version: t -> int = "caml_js_get_version"
  external set_version: t -> int -> unit = "caml_js_set_version"
end

module Val = struct
  type t = jsval
  external runtime: t -> Rt.t = "caml_js_rt_of_value"
  external lambda: Ctx.t -> string -> (Ctx.t -> t array -> t) -> t = "caml_js_new_function"
  external int: int -> t = "%identity"
  external special_csts: int -> t = "caml_js_special_csts"
  external string: Ctx.t -> string -> t = "caml_js_new_string"
  external float: Ctx.t -> float -> t = "caml_js_new_double"

  external to_string: Ctx.t -> t -> string = "caml_js_to_string"
  external to_object: Ctx.t -> t -> t = "caml_js_to_object"
  external to_bool: Ctx.t -> t -> bool = "caml_js_to_bool"
  external to_number: Ctx.t -> t -> float = "caml_js_to_number"

  let vfalse = special_csts 0
  let vtrue = special_csts 1
  let vnull = special_csts 2
  let vvoid = special_csts 3

  let bool b = if b then vtrue else vfalse

  let to_string ctx (v : t) =
    if Obj.is_int (Obj.repr v) then string_of_int (Obj.magic v)
    else to_string ctx v


  external is_object: Ctx.t -> t -> bool = "caml_js_is_object"
  external is_boolean: Ctx.t -> t -> bool = "caml_js_is_boolean"
  external is_int: Ctx.t -> t -> bool = "caml_js_is_int"
  external is_null: Ctx.t -> t -> bool = "caml_js_is_null"
  external is_void: Ctx.t -> t -> bool = "caml_js_is_void"
  external is_string: Ctx.t -> t -> bool = "caml_js_is_string"
  external is_number: Ctx.t -> t -> bool = "caml_js_is_number"
  external is_double: Ctx.t -> t -> bool = "caml_js_is_double"
  external is_array: Ctx.t -> t -> bool = "caml_js_is_array"

  external get_boolean: Ctx.t -> t -> bool = "caml_js_get_boolean"
  external get_int: Ctx.t -> t -> int = "caml_js_get_int"
  external get_string: Ctx.t -> t -> string = "caml_js_get_string"
  external get_double: Ctx.t -> t -> float = "caml_js_get_double"

    (* Functions below assume the first arg of type t is actually
       an object. *)
  external get_global: Ctx.t -> t = "caml_js_get_global_object"
  external set_prop: Ctx.t -> t -> string -> t -> unit = "caml_js_set_prop"
  external get_prop: Ctx.t -> t -> string -> t = "caml_js_get_prop"
  external set_elem: Ctx.t -> t -> int -> t -> unit = "caml_js_set_elem"
  external get_elem: Ctx.t -> t -> int -> t = "caml_js_get_elem"
  external enumerate: Ctx.t -> t -> t list = "caml_js_enumerate"
  external create: Ctx.t -> ?proto:t -> ?parent:t -> 
    objops option -> t = "caml_js_new_object"

  external array: Ctx.t -> t = "caml_js_new_array"
end

external eval_script: Ctx.t -> Val.t -> string -> Val.t = "caml_js_evaluate_script"


(* Simplified API *)

let getProp cx prop v =
  Printf.printf "Get prop %s = %s\n" prop (Val.to_string cx v);
  v

type 'a active = {
  setter: string -> 'a -> 'a;
  getter: string -> 'a -> 'a;
}
   
let mkobjops new_jsobj active =
  match active with None -> None | Some cls ->
    Some ((fun cx_run prop v -> 
	     (cls.getter prop (new_jsobj cx_run v)) # v),
	  (fun cx_run prop v -> 
	     (cls.setter prop (new_jsobj cx_run v)) # v))


class jsobj cx o = object(this:'this)
  method v = o
  method set n (v : jsobj) = Val.set_prop cx o n (v#v)
  method get n = new jsobj cx (Val.get_prop cx o n)
  method set_idx n (v : jsobj) = Val.set_elem cx o n (v#v)
  method get_idx n = new jsobj cx (Val.get_elem cx o n)
  method enumerate = List.map (fun v -> new jsobj cx v) (Val.enumerate cx o)

  method new_object_gen ?proto ?parent ?active () =
    let proto = 
      match proto with None -> None | Some (x : jsobj) -> Some (x # v) in
    let parent = 
      match parent with None -> None | Some (x : jsobj) -> Some (x # v) in
    let cls = mkobjops (new jsobj) active in
    new jsobj cx (Val.create cx ?proto ?parent cls)

  method new_child ?proto ?active () =
    this # new_object_gen ?proto ~parent:(this :> jsobj) ?active ()

  method new_object ?proto ?active () =
    this # new_object_gen ?proto ?active ()

  method eval s = new jsobj cx (eval_script cx o s)

  method to_string = Val.to_string cx o
  method to_object = new jsobj cx (Val.to_object cx o)
  method to_bool = Val.to_bool cx o
  method to_float = Val.to_number cx o
  method to_int = int_of_float (Val.to_number cx o)

  (* Value creator *)
  method lambda ?(name="anonymous") (f : jsobj -> jsobj array -> jsobj) = 
    new jsobj cx 
      (Val.lambda cx name 
	 (fun cx_run a ->
	    let a = Array.map (new jsobj cx) a in
	    (f (new jsobj cx_run (Val.get_global cx_run)) a) # v))

  method string s = new jsobj cx (Val.string cx s)
  method float f = new jsobj cx (Val.float cx f)
  method int i = new jsobj cx (Val.int i)
  method bool b = new jsobj cx (Val.bool b)
  method null = new jsobj cx Val.vnull
  method void = new jsobj cx Val.vvoid
  method _true = new jsobj cx Val.vtrue
  method _false = new jsobj cx Val.vfalse
  method array a = 
    let o = new jsobj cx (Val.array cx) in
    for i = 0 to Array.length a - 1 do o # set_idx i a.(i) done;
    o

  method is_object = Val.is_object cx o
  method is_bool = Val.is_boolean cx o
  method is_int = Val.is_int cx o
  method is_null = Val.is_null cx o
  method is_void = Val.is_void cx o
  method is_string = Val.is_string cx o
  method is_number = Val.is_number cx o
  method is_float = Val.is_double cx o
  method is_array = Val.is_array cx o

  method get_bool = Val.get_boolean cx o
  method get_int = Val.get_int cx o
  method get_string = Val.get_string cx o
  method get_float = Val.get_double cx o

  method destroy_runtime = Rt.destroy (Val.runtime o)
  method version = Ctx.version cx
  method set_version v = Ctx.set_version cx v

  method new_context ?active () = 
    let cx = Ctx.create (Ctx.runtime cx) (mkobjops (new jsobj) active) in
    new jsobj cx (Val.get_global cx)

end

let global_obj cx = new jsobj cx (Val.get_global cx)
let new_global_obj ?active () = global_obj (Ctx.create 
					      (Rt.create ())
					      (mkobjops (new jsobj) active))

external implementation_version: unit -> string = "caml_js_implementation_version"
