(** Binding to SpiderMonkey's implementation of Javascript. *)

(** Author: Alain Frisch. *)

(** It is possible to manage several runtimes at the same time.
    Each runtime is implicitly bound to an object created
    by a call to [new_global_obj]. All the values created
    from this object (directly or not) are also attached
    to this runtime.

    It is not legal to mix values from several runtimes
    (e.g. to set the value of a property of an object A
    to a value B, where A and B are in different runtimes).
    The exception [InvalidRuntime] is raised when this rule is broken.
    
    In order to avoid memory leaks, it is necessary to call 
    the [destroy_runtime] on an arbitrary value of the runtime.
    After this call, it is illegal to use values from this runtime
    (actually, only calling closures and registering new ones
    is prohibited).  An exception [RuntimeDestroyed] is raised
    when this rule is broken. *)
   

type jsval

(** Encapsulation of Javascript values. *)
class type jsobj = object
  (** Evaluation of Javascript expressions. *)

  method eval : string -> jsobj
    (** Evaluate a Javascript expression in the context of the current
	object.
	Raise [InvalidType] if the value is not an object. *)

  (** Access to properties. *)

  method get : string -> jsobj
    (** Get the value of a named property. Raise [InvalidType]
	if the value is not an object. *)
  method set : string -> jsobj -> unit
    (** Set the value of a named property.  Raise [InvalidType]
	if the value is not an object. *)

  method get_idx : int -> jsobj
    (** Get the value of a numeric property.  Raise [InvalidType]
	if the value is not an object. *)
  method set_idx : int -> jsobj -> unit
    (** Set the value of a numeric property.  Raise [InvalidType]
	if the value is not an object. *)

  (** Creation of values. *)

  method new_child : ?proto:jsobj -> unit -> jsobj
    (** Create a new object whose parent is the current object.
	Raise [InvalidType] if the value is not an object. *)
  method new_object : ?proto:jsobj -> unit -> jsobj
    (** Create a new object with no parent.
	Raise [InvalidType] if the value is not an object. *)

  method lambda : ?name:string -> (jsobj -> jsobj array -> jsobj) -> jsobj
    (** Create a closure value with an optional name (for decompiling).
	The first argument of the closure is the global object
	(not the current one) of the evaluation context. The second
	argument contains all the arguments passed to the function.

	A side effect of calling this method is to register the
	closure in a global table (of the current runtime). 
	Only a call to [destroy_runtime] will free this table. *)

  method string : string -> jsobj
  method int : int -> jsobj
  method bool : bool -> jsobj
  method _true : jsobj
  method _false : jsobj
  method null : jsobj
  method void : jsobj
  method float : float -> jsobj
  method array : jsobj array -> jsobj

  (** Value inspection. *)

  method is_object : bool
  method is_bool : bool
  method is_int : bool
  method is_null : bool
  method is_void : bool
  method is_string : bool
  method is_number : bool
  method is_float : bool
  method is_array : bool

  method get_int : int
    (** Raise [InvalidType] is the value is not an integer. *)
  method get_bool : bool
    (** Raise [InvalidType] is the value is not a boolean. *)
  method get_string : string
    (** Raise [InvalidType] is the value is not a string. *)
  method get_float : float
    (** Raise [InvalidType] is the value is not a double. *)


  (** Value conversion. *)

  method to_string : string
  method to_object : jsobj
  method to_bool : bool
  method to_float : float
  method to_int : int

(** Misc. *)

  method destroy_runtime : unit
    (** Free the internal table of all the registered closures.
	It is possible to use this method on any value of the
	runtime. After calling it, it is no longer possible to
	create or to call closures in this runtime. *)

  method new_context : jsobj
    (** Create a new evaluation context with a fresh global object,
	in the same runtime. Values can be exchanged between contexts 
	of the same runtime. *)

  method version : int
    (** The JS version supported by the current context.
	Possible values: 100 (JS 1.0), 110 (JS 1.1),
	120 (JS 1.2), 130 (JS 1.3), 0 (default), -1 (unknown). *)

  method set_version : int -> unit
    (** Change the JS version supported by the current context.
	Possible values: 100,110,120,130. *)


(**/**)
  method v : jsval
end

val new_global_obj : unit -> jsobj
  (** Create a new global object. *)

val implementation_version : unit -> string

module Error : sig
  (** Javascript errors. *)

  type t = { 
    message: string; (** The error message. *)
    filename: string option; (** (currently always None). *)
    line: string option; (** A copy of the line where the error occured. *)
    lineno: int; (** Line number. *)
    colno: int; (** Column number. *)
  }

  exception Message of t

  exception RuntimeDestroyed
    (** Raised after a call to [destroy_runtime] when 
	a new closure is created ([lambda] method) or when
	a JS script call a closure. *)
    
  exception InvalidRuntime
    (** Raised when values from different runtimes are mixed together. *)
    
  exception InvalidType
    (** Raised when a value of some JS type is used as a value
	of another type. *)
end
