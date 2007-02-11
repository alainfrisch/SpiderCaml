open SpiderCaml

let () =
  let glb = new_global_obj () in
  let o = glb # new_object () in
  glb # set "self" o;
  o # set "close" 
    (o # lambda (fun _ _ -> print_endline "CLOSED"; o # null));
  ignore (glb # eval "self.close(1,2);")
