def f
  str = [
    "/*%%%*/\n",
    "%token\n",
    "/*%\n",
    "%token <val>\n",
    "%*/\n",
    "%token <id>   tIDENTIFIER tFID tGVAR tIVAR tCONSTANT tCVAR tLABEL\n",
    "%token <node> tINTEGER tFLOAT tRATIONAL tIMAGINARY tSTRING_CONTENT tCHAR\n",
    "%token <node> tNTH_REF tBACK_REF\n",
    "%token <num>  tREGEXP_END\n",
    "%type <id>   fsym keyword_variable user_variable sym symbol operation operation2 operation3\n",
    "%type <id>   cname fname op f_rest_arg f_block_arg opt_f_block_arg f_norm_arg f_bad_arg\n",
    "%type <id>   f_kwrest f_label f_arg_asgn\n",
    "/*%%%*/\n",
    "/*%\n",
    "%type <val> program reswords then do dot_or_colon\n",
    "%*/\n",
    "%token END_OF_INPUT 0	'end-of-input'\n",
    "%token tUPLUS		130 'unary+'\n",
    "%token tUMINUS		131 'unary-'\n"
  ]

  i = 0
  out = ""
  while line = str[i]
    case line
    when %r</\*%%%\*/>
      out << '#if 0' << $/
    when %r</\*%c%\*/>
      out << '/*' << $/
    when %r</\*%c>
      out << '*/' << $/
    when %r</\*%>
      out << '#endif' << $/
    when %r<%\*/>
      out << $/
    when /\A%%/
      out << '%%' << $/
      return
    else
      out << line
    end
    i += 1
  end
  return out
end
puts f()
