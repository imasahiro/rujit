def f(&block)
  a = 10

  x = block.call binding
  puts x
  b = 20
  y = block.call binding
  puts y
  return x
end

f {|bind|
  # puts b.local_variables
  # puts b.receiver
  puts bind.local_variables.join(",")
  puts bind.receiver
  bind.local_variable_set(:block, proc {|b| nil})
  10
  # eval "block = proc {|b| nil}", bind

    #rb_define_method(rb_cBinding, "local_variables", bind_local_variables, 0);
    #rb_define_method(rb_cBinding, "local_variable_get", bind_local_variable_get, 1);
    #rb_define_method(rb_cBinding, "local_variable_set", bind_local_variable_set, 2);
    #rb_define_method(rb_cBinding, "local_variable_defined?", bind_local_variable_defined_p, 1);
    #rb_define_method(rb_cBinding, "receiver", bind_receiver, 0);
    #rb_define_global_function("binding", rb_f_binding, 0);

}
