open SpiderCaml

let report_error_message = function
  | { Error.line = Some l } as e ->
      Printf.printf "JS error '%s', at line %i.\n%s\n%s^\n"
	e.Error.message
	e.Error.lineno
	l
	(String.make e.Error.colno ' ')
  | e ->
      Printf.printf "JS error '%s', at line %i.\n"
	e.Error.message
	e.Error.lineno

let rec main_loop glb =
  print_string "> "; flush stdout;
  (try
     let s = read_line () in
     if s = "" then raise Exit;
     let v = glb # eval s in
     Printf.printf ": %s\n" (v # to_string);
   with Error.Message e -> report_error_message e
     | End_of_file -> raise Exit);
  main_loop glb

let () =
  print_endline "This is a demonstration of SpiderCaml.";
  Printf.printf "SpiderMonkey implementation: %s\n" (implementation_version());
  Printf.printf "built-in functions: exit,print\n";
  let glb = new_global_obj () in
  let def name f =
    glb # set name (glb # lambda ~name f) in
  def "exit" (fun _ _ -> raise Exit);
  def "print" (fun g a ->
		 for i = 0 to Array.length a - 1 do
		   Printf.printf "[%i] %s\n" i (a.(i) # to_string);
		 done;
		 g # null
	      );
  try main_loop glb
  with Exit -> glb # destroy_runtime
