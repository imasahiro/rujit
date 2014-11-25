#!ruby

$indent_level = 0

class LValue
  attr_accessor :name, :type
  def initialize(name, type = nil)
    @name = name
    @type = type
  end
  def to_s
    @name
  end
end

class Yarv2Lir
  def self.match(op, &block)
    puts "static void record_#{op.to_s}(trace_recorder_t *rec, jit_event_t *e)"
    puts "{"
    $indent_level += 1
    yield
    $indent_level -= 1
    puts "}"
  end

  def self.indent
    "  " * $indent_level
  end

  def self.emit(opname, *arg)
    "EmitIR(#{[opname, *arg].map{|e| e.to_s}.join(", ")});"
  end

  def self.operand type, idx
    LValue.new("(#{type})GET_OPERAND(#{idx});", type)
  end

  def self.take_snapshot
    "take_snapshot(rec);"
  end

  def self.pop
    "_POP();"
  end

  def self.topn idx
    "TOPN(#{idx});"
  end

  def self.to_mid(sym)
    return "idPLUS"      if sym == '+'
    return "idMINUS"     if sym == '-'
    return "idMULT"      if sym == '*'
    return "idDIV"       if sym == '/'
    return "idMOD"       if sym == '%'
    return "idPow"       if sym == '**'
    return "idLT"        if sym == '<'
    return "idLTLT"      if sym == '<<'
    return "idLE"        if sym == '<='
    return "idGT"        if sym == '>'
    return "idGE"        if sym == '>='
    return "idEq"        if sym == '=='
    return "idNeq"       if sym == '!='
    return "idNot"       if sym == '!'
    return "idNot"       if sym == "not";
    return "idAREF"      if sym == "[]"
    return "idASET"      if sym == "[]="
    return "idLength"    if sym == "length"
    return "idSize"      if sym == "size"
    return "idEmptyP"    if sym == "empty?"
    return "idSucc"      if sym == "succ"
    return "idEqTilde"   if sym == "=~"
    return "rb_intern(\"#{sym}\")"
  end

  def self.stringify(sym)
    return "JIT_BOP_PLUS"      if sym == '+'
    return "JIT_BOP_MINUS"     if sym == '-'
    return "JIT_BOP_MULT"      if sym == '*'
    return "JIT_BOP_DIV"       if sym == '/'
    return "JIT_BOP_MOD"       if sym == '%'
    return "JIT_BOP_POW"       if sym == '**'
    return "JIT_BOP_LT"        if sym == '<'
    return "JIT_BOP_LTLT"      if sym == '<<'
    return "JIT_BOP_LE"        if sym == '<='
    return "JIT_BOP_GT"        if sym == '>'
    return "JIT_BOP_GE"        if sym == '>='
    return "JIT_BOP_EQ"        if sym == '=='
    return "JIT_BOP_NEQ"       if sym == '!='
    return "JIT_BOP_NOT"       if sym == '!'
    return "JIT_BOP_NOT"       if sym == "not";
    return "JIT_BOP_AREF"      if sym == "[]"
    return "JIT_BOP_ASET"      if sym == "[]="
    return "JIT_BOP_LENGTH"    if sym == "length"
    return "JIT_BOP_SIZE"      if sym == "size"
    return "JIT_BOP_EMPTY_P"   if sym == "empty?"
    return "JIT_BOP_SUCC"      if sym == "succ"
    return "JIT_BOP_MATCH"     if sym == "=~"
    return "JIT_BOP_FREEZE"    if sym == "freeze"

    return "JIT_BOP_NEG"       if sym == '-@'
    return "JIT_BOP_AND"       if sym == '&'
    return "JIT_BOP_OR"        if sym == '|'
    return "JIT_BOP_XOR"       if sym == '^'
    return "JIT_BOP_INV"       if sym == '~'
    return "JIT_BOP_RSHIFT"    if sym == '>>'

    return "JIT_BOP_TO_F"    if sym == 'to_f'
    return "JIT_BOP_TO_I"    if sym == 'to_i'
    return "JIT_BOP_TO_S"    if sym == 'to_s'
    # math
    return "JIT_BOP_SIN"       if sym == 'sin'
    return "JIT_BOP_COS"       if sym == 'cos'
    return "JIT_BOP_TAN"       if sym == 'tan'
    return "JIT_BOP_EXPR"      if sym == 'expr'
    return "JIT_BOP_SQRT"      if sym == 'sqrt'
    return "JIT_BOP_LOG2"      if sym == 'log2'
    return "JIT_BOP_LOG10"     if sym == 'log10'
    return "JIT_BOP_" + sym.upcase
  end

  def self.push val
    print indent
    puts "_PUSH(#{val.to_s});"
    print indent
    puts "return;"
  end

  def self.guard(type, *arg, &block)
    print indent
    $indent_level += 1
    if type.kind_of? String
      type, mname = type.split(".")
      puts "if (JIT_OP_UNREDEFINED_P(#{stringify(mname)}, #{type.upcase}_REDEFINED_OP_FLAG)) {"
    else
      puts "if (IS_#{type}(#{arg[0]})) {"
      print indent
      puts emit "GuardType#{type}".to_sym, arg[0], arg[1]
    end
    yield
    $indent_level -= 1
    print indent
    puts "}"
  end

  def self.if_(type, val, &block)
    print indent
    if type == :argc
      puts "if (ci->#{type} == #{val}) {"
    elsif type == :mid
      puts "if (ci->mid == #{to_mid(val)}) {"
    elsif type == :method_type
      puts "if (ci->me && ci->me->def->type == VM_METHOD_TYPE_#{val.upcase}) {"
    else
      puts "if (#{type}) {"
    end
    $indent_level += 1
    yield
    $indent_level -= 1
    print indent
    puts "}"
  end

  def self.other(&block)
    print indent
    puts "L_other: {"
    $indent_level += 1
    yield
    $indent_level -= 1
    print indent
    puts "}"
  end

  def self.emit_get_prop recv
  end

  def self.emit_set_prop recv, obj
  end

  def self.emit_call_method
  end

  class Local
    def method_missing(name, *arg, &block)
      name = name.to_s
      if name.end_with?("=")
        name = name.gsub("=", "")
        print "  " * $indent_level
        if name == "snapshot"
          puts "jit_snapshot_t *#{name} = #{arg[0]}"
        elsif name == "result"
          puts arg[0]
        elsif name.start_with?("v") and name.size > 1
          puts "VALUE #{name} = #{arg[0]}"
        elsif arg[0].kind_of? LValue
          puts "#{arg[0].type} #{name} = #{arg[0].to_s}"
        else
          puts "lir_t #{name} = #{arg[0]}"
        end
        LValue.new(name)
      else
        LValue.new(name)
      end
    end
    def swap(v1, v2)
      v1.name, v2.name = v2.name, v1.name
    end
  end

  local = Local.new

  match(:nop) {
  }

  match(:getlocal) {
    local.a = operand :int, 1
    local.b = operand :int, 2
    local.v = emit :EnvLoad, local.a, local.b
    push local.v
  }
  match(:setlocal) {
    local.a = operand :int, 1
    local.b = operand :int, 2
    local.v = pop
    local.result =emit :EnvStore, local.a, local.b, local.v
  }
  # match(:getspecial)
  # match(:setspecial)
  # match(:getinstancevariable)
  # match(:setinstancevariable)
  # match(:getclassvariable)
  # match(:setclassvariable)
  # match(:getconstant)
  # match(:setconstant)

  match(:getglobal) {
    local.entry = operand :GENTRY, 1
    local.v = emit :GetGlobal, local.entry
    push local.v
  }
  match(:setglobal) {
    local.entry = operand :GENTRY, 1
    local.v = pop
    local.result =emit :SetGlobal, local.entry, local.v
  }

  # match(:putnil)
  # match(:putself)
  # match(:putobject)
  # match(:putspecialobject)
  # match(:putiseq)
  # match(:putstring)
  # match(:concatstrings)
  # match(:tostring)
  # match(:toregexp)
  # match(:newarray)
  # match(:duparray)
  # match(:expandarray)
  # match(:concatarray)
  # match(:splatarray)
  # match(:newhash)
  # match(:newrange)
  # match(:pop)
  # match(:dup)
  # match(:dupn)
  # match(:swap)
  # match(:reput)
  # match(:topn)
  # match(:setn)
  # match(:adjuststack)
  # match(:defined)
  # match(:checkmatch)
  # match(:checkkeyword)
  # match(:trace)
  # match(:defineclass)
  # match(:send)

  match(:opt_str_freeze) {
    local.snapshot = take_snapshot
    local.recv = pop
    guard("String.freeze", local.snapshot) {
      push local.recv
    }
  }

  match(:opt_send_without_block) {
    local.snapshot = take_snapshot
    local.recv = pop
    local.ci = operand :CALL_INFO, 0
    local.vrecv = topn(0)
    guard(:Fixnum, local.recv, local.vrecv, local.snapshot) {
      if_(:argc, 1) {
        if_(:mid, "-@") {
          guard("Fixnum.-@") {
            local.v = emit :FixnumNeg, local.recv
            push local.v
          }
        }
        if_(:mid, "~") {
          guard("Fixnum.~") {
            local.v = emit :FixnumComplement, local.recv
            push local.v
          }
        }
        if_(:mid, "to_f") {
          guard("Fixnum.to_f") {
            local.v = emit :FixnumToFloat, local.recv
            push local.v
          }
        }
        if_(:mid, "to_s") {
          guard("Fixnum.to_s") {
            local.v = emit :FixnumToString, local.recv
            push local.v
          }
        }
        if_(:mid, "to_i") {
          guard("Fixnum.to_i") {
            push local.recv
          }
        }
      }
      if_(:argc, 2) {
        local.obj = pop
        local.swap(local.recv, local.obj)
        if_(:mid, "**") {
          guard("Fixnum.**") {
            local.v = emit :FixnumPow, local.recv, local.obj
            push local.v
          }
        }
        if_(:mid, "**") {
          guard("Fixnum.**") {
            local.v = emit :FixnumPow, local.recv, local.obj
            push local.v
          }
        }
        if_(:mid, "**") {
          guard("Fixnum.**") {
            local.v = emit :FixnumPow, local.recv, local.obj
            push local.v
          }
        }
        if_(:mid, "&") {
          guard("Fixnum.&") {
            local.v = emit :FixnumAnd, local.recv, local.obj
            push local.v
          }
        }
        if_(:mid, "|") {
          guard("Fixnum.|") {
            local.v = emit :FixnumOr, local.recv, local.obj
            push local.v
          }
        }
        if_(:mid, "^") {
          guard("Fixnum.^") {
            local.v = emit :FixnumXor, local.recv, local.obj
            push local.v
          }
        }
        if_(:mid, "<<") {
          guard("Fixnum.<<") {
            local.v = emit :FixnumLshift, local.recv, local.obj
            push local.v
          }
        }
        if_(:mid, ">>") {
          guard("Fixnum.>>") {
            local.v = emit :FixnumRshift, local.recv, local.obj
            push local.v
          }
        }
      }
    }
    guard(:Float, local.recv, local.vrecv, local.snapshot) {
      if_(:argc, 1) {
        if_(:mid, "-@") {
          guard("Float.-@") {
            local.v = emit :FloatNeg, local.recv
            push local.v
          }
        }
        if_(:mid, "to_f") {
          guard("Float.to_f") {
            push local.recv
          }
        }
        if_(:mid, "to_s") {
          guard("Float.to_s") {
            local.v = emit :FloatToString, local.recv
            push local.v
          }
        }
        if_(:mid, "to_i") {
          guard("Float.to_i") {
            local.v = emit :FloatToFixnum, local.recv
            push local.v
          }
        }
      }
      if_(:argc, 2) {
        local.obj = pop
        local.swap(local.recv, local.obj)
        if_(:mid, "**") {
          guard("Float.**") {
            local.v = emit :FloatPow, local.recv, local.obj
            push local.v
          }
        }
        if_(:mid, "**") {
          guard("Float.**") {
            local.v = emit :FloatPow, local.recv, local.obj
            push local.v
          }
        }
      }
    }

    guard(:String, local.recv, local.vrecv, local.snapshot) {
      if_(:mid, "to_f") {
        guard("String.to_f") {
          local.v = emit :StringToFloat, local.recv
          push local.recv
        }
      }
      if_(:mid, "to_s") {
        guard("String.to_s") {
          push local.recv
        }
      }
      if_(:mid, "to_i") {
        guard("String.to_i") {
          local.v = emit :StringToFixnum, local.recv
          push local.v
        }
      }
    }
    guard(:Math, local.recv, local.vrecv, local.snapshot) {
      if_(:argc, 2) {
        local.obj = pop
        local.vobj = topn(1)
        local.swap(local.recv, local.obj)
        local.swap(local.vrecv, local.vobj)
        if_(:mid, "sin") {
          guard("Math.sin") {
            guard(:Fixnum, local.obj, local.vobj) {
              local.v1 = emit :Fix2Float, local.obj
              local.v = emit :MathSin, local.recv, local.v1
              push local.v
            }
            guard(:Float, local.obj, local.vobj) {
              local.v = emit :MathSin, local.recv, local.obj
              push local.v
            }
          }
        }
        if_(:mid, "cos") {
          guard("Math.cos") {
            guard(:Fixnum, local.obj, local.vobj) {
              local.v1 = emit :Fix2Float, local.obj
              local.v = emit :MathCos, local.recv, local.v1
              push local.v
            }
            guard(:Float, local.obj, local.vobj) {
              local.v = emit :MathCos, local.recv, local.obj
              push local.v
            }
          }
        }
        if_(:mid, "tan") {
          guard("Math.tan") {
            guard(:Fixnum, local.obj, local.vobj) {
              local.v1 = emit :Fix2Float, local.obj
              local.v = emit :MathTan, local.recv, local.v1
              push local.v
            }
            guard(:Float, local.obj, local.vobj) {
              local.v = emit :MathTan, local.recv, local.obj
              push local.v
            }
          }
        }
        if_(:mid, "exp") {
          guard("Math.exp") {
            guard(:Fixnum, local.obj, local.vobj) {
              local.v1 = emit :Fix2Float, local.obj
              local.v = emit :MathExp, local.recv, local.v1
              push local.v
            }
            guard(:Float, local.obj, local.vobj) {
              local.v = emit :MathExp, local.recv, local.obj
              push local.v
            }
          }
        }
        if_(:mid, "sqrt") {
          guard("Math.sqrt") {
            guard(:Fixnum, local.obj, local.vobj) {
              local.v1 = emit :Fix2Float, local.obj
              local.v = emit :MathSqrt_i, local.recv, local.v1
              push local.v
            }
            guard(:Float, local.obj, local.vobj) {
              local.v = emit :MathSqrt, local.recv, local.obj
              push local.v
            }
          }
        }
        if_(:mid, "log10") {
          guard("Math.log10") {
            guard(:Fixnum, local.obj, local.vobj) {
              local.v1 = emit :Fix2Float, local.obj
              local.v = emit :MathLog10, local.recv, local.v1
              push local.v
            }
            guard(:Float, local.obj, local.vobj) {
              local.v = emit :MathLog10, local.recv, local.obj
              push local.v
            }
          }
        }
        if_(:mid, "log2") {
          guard("Math.log2") {
            guard(:Fixnum, local.obj, local.vobj) {
              local.v1 = emit :Fix2Float, local.obj
              local.v = emit :MathLog2, local.recv, local.v1
              push local.v
            }
            guard(:Float, local.obj, local.vobj) {
              local.v = emit :MathLog2, local.recv, local.obj
              push local.v
            }
          }
        }
      }
    }
    guard(:Array, local.recv, local.vrecv, local.snapshot) {
      local.recv = pop
      if_(:argc, 1) {
        if_(:mid, "length") {
          guard("Array.length") {
            local.v = emit :ArrayLength, local.recv
            push local.v
          }
        }
        if_(:mid, "[]") {
          guard("Array.[]") {
            local.v = emit :ArrayGet, local.recv
            push local.v
          }
        }
      }
      if_(:argc, 2) {
        local.obj = pop
        local.swap(local.recv, local.obj)
        if_(:mid, "[]") {
          guard("Array.[]=") {
            local.v = emit :ArraySet, local.recv, local.obj
            push local.v
          }
        }
      }
    }
    if_(:method_type, :ivar) {
      guard(:Object, local.recv, local.vrecv) {
        local.v = emit_get_prop local.recv
      }
    }
    if_(:method_type, :attrset) {
      guard(:Object, local.recv, local.vrecv) {
        local.obj = pop
        local.swap(local.recv, local.obj)
        local.v = emit_set_prop local.recv, local.obj
        push local.v
      }
    }
    other {
      local.result =emit_call_method
    }
  }

  ## match(:invokesuper)
  ## match(:invokeblock)
  ## match(:leave)
  ## match(:throw)
  ## match(:jump)
  ## match(:branchif)
  ## match(:branchunless)
  ## match(:getinlinecache)
  ## match(:setinlinecache)
  ## match(:once)
  ## match(:opt_case_dispatch)

  match(:opt_plus) {
    local.snapshot = take_snapshot
    local.obj  = pop
    local.recv = pop
    local.vobj  = topn(0)
    local.vrecv = topn(1)
    guard(:Fixnum, local.recv, local.vrecv, local.snapshot) {
      guard(:Fixnum, local.obj, local.vobj, local.snapshot) {
        guard("Fixnum.+") {
          local.v = emit :FixnumAddOverflow, local.recv, local.obj
          push local.v
        }
      }
    }
    guard(:Float, local.recv, local.vrecv, local.snapshot) {
      guard(:Float, local.obj, local.vobj, local.snapshot) {
        guard("Float.+") {
          local.v = emit :FloatAdd, local.recv, local.obj
          push local.v
        }
      }
      guard(:Fixnum, local.obj, local.vobj, local.snapshot) {
        guard("Float.+") {
          local.obj  = emit :Fixnum2Float, local.obj
          local.v = emit :FloatAdd, local.recv, local.obj
          push local.v
        }
      }
    }
    guard(:String, local.recv, local.vrecv, local.snapshot) {
      guard(:String, local.obj, local.vobj, local.snapshot) {
        guard("String.+") {
          local.v = emit :StringAdd, local.recv, local.obj
          push local.v
        }
      }
    }
    guard(:Array, local.recv, local.vrecv, local.snapshot) {
      guard(:Array, local.obj, local.vobj, local.snapshot) {
        guard("Array.+") {
          local.v = emit :ArrayConcat, local.recv, local.obj
          push local.v
        }
      }
    }
    other {
      local.v = emit :CallMethod, local.recv, local.obj
      push local.v
    }
  }

  match(:opt_minus) {
    local.snapshot = take_snapshot
    local.obj  = pop
    local.recv = pop
    local.vobj  = topn(0)
    local.vrecv = topn(1)
    guard(:Fixnum, local.recv, local.vrecv, local.snapshot) {
      guard(:Fixnum, local.obj, local.vobj, local.snapshot) {
        guard("Fixnum.-") {
          local.v = emit :FixnumSubOverflow, local.recv, local.obj
          push local.v
        }
      }
    }
    guard(:Float, local.recv, local.vrecv, local.snapshot) {
      guard(:Float, local.obj, local.vobj, local.snapshot) {
        guard("Float.-") {
          local.v = emit :FloatSub, local.recv, local.obj
          push local.v
        }
      }
      guard(:Fixnum, local.obj, local.vobj, local.snapshot) {
        guard("Float.-") {
          local.obj  = emit :Fixnum2Float, local.obj
          local.v = emit :FloatSub, local.recv, local.obj
          push local.v
        }
      }
    }
    other {
      local.v = emit :CallMethod, local.recv, local.obj
      push local.v
    }
  }
  match(:opt_mult) {
    local.snapshot = take_snapshot
    local.obj  = pop
    local.recv = pop
    local.vobj  = topn(0)
    local.vrecv = topn(1)
    guard(:Fixnum, local.recv, local.vrecv, local.snapshot) {
      guard(:Fixnum, local.obj, local.vobj, local.snapshot) {
        guard("Fixnum.*") {
          local.v = emit :FixnumMulOverflow, local.recv, local.obj
          push local.v
        }
      }
    }
    guard(:Float, local.recv, local.vrecv, local.snapshot) {
      guard(:Float, local.obj, local.vobj, local.snapshot) {
        guard("Float.*") {
          local.v = emit :FloatMul, local.recv, local.obj
          push local.v
        }
      }
      guard(:Fixnum, local.obj, local.vobj, local.snapshot) {
        guard("Float.*") {
          local.obj  = emit :Fixnum2Float, local.obj
          local.v = emit :FloatMul, local.recv, local.obj
          push local.v
        }
      }
    }
    other {
      local.v = emit :CallMethod, local.recv, local.obj
      push local.v
    }
  }

  match(:opt_div) {
    local.snapshot = take_snapshot
    local.obj  = pop
    local.recv = pop
    local.vobj  = topn(0)
    local.vrecv = topn(1)
    guard(:Fixnum, local.recv, local.vrecv, local.snapshot) {
      guard(:Fixnum, local.obj, local.vobj, local.snapshot) {
        guard("Fixnum./") {
          local.v = emit :FixnumDivOverflow, local.recv, local.obj
          push local.v
        }
      }
    }
    guard(:Float, local.vrecv, local.snapshot) {
      guard(:Float, local.obj, local.vobj, local.snapshot) {
        guard("Float./") {
          local.v = emit :FloatDiv, local.recv, local.obj
          push local.v
        }
      }
      guard(:Fixnum, local.obj, local.vobj, local.snapshot) {
        guard("Float./") {
          local.obj  = emit :Fixnum2Float, local.obj
          local.v = emit :FloatDiv, local.recv, local.obj
          push local.v
        }
      }
    }
    other {
      local.v = emit :CallMethod, local.recv, local.obj
      push local.v
    }
  }

  match(:opt_mod) {
    local.snapshot = take_snapshot
    local.obj  = pop
    local.recv = pop
    local.vobj  = topn(0)
    local.vrecv = topn(1)
    guard(:Fixnum, local.recv, local.vrecv, local.snapshot) {
      guard(:Fixnum, local.obj, local.vobj, local.snapshot) {
        guard("Fixnum.%") {
          local.v = emit :FixnumModOverflow, local.recv, local.obj
          push local.v
        }
      }
    }
    guard(:Float, local.vrecv, local.snapshot) {
      guard(:Float, local.obj, local.vobj, local.snapshot) {
        guard("Float.%") {
          local.v = emit :FloatMod, local.recv, local.obj
          push local.v
        }
      }
      guard(:Fixnum, local.obj, local.vobj, local.snapshot) {
        guard("Float.%") {
          local.obj  = emit :Fixnum2Float, local.obj
          local.v = emit :FloatMod, local.recv, local.obj
          push local.v
        }
      }
    }
    other {
      local.v = emit :CallMethod, local.recv, local.obj
      push local.v
    }
  }
  match(:opt_eq) {
    local.snapshot = take_snapshot
    local.obj  = pop
    local.recv = pop
    local.vobj  = topn(0)
    local.vrecv = topn(1)
    guard(:Fixnum, local.recv, local.vrecv, local.snapshot) {
      guard(:Fixnum, local.obj, local.vobj, local.snapshot) {
        guard("Fixnum.==") {
          local.v = emit :FixnumEq, local.recv, local.obj
          push local.v
        }
      }
    }
    guard(:Float, local.vrecv, local.snapshot) {
      guard(:Float, local.obj, local.vobj, local.snapshot) {
        guard("Float.==") {
          local.v = emit :FloatEq, local.recv, local.obj
          push local.v
        }
      }
    }
    other {
      local.v = emit :CallMethod, local.recv, local.obj
      push local.v
    }
  }
  match(:opt_neq) {
    local.snapshot = take_snapshot
    local.obj  = pop
    local.recv = pop
    local.vobj  = topn(0)
    local.vrecv = topn(1)
    guard(:Fixnum, local.recv, local.vrecv, local.snapshot) {
      guard(:Fixnum, local.obj, local.vobj, local.snapshot) {
        guard("Fixnum.!=") {
          local.v = emit :FixnumNe, local.recv, local.obj
          push local.v
        }
      }
    }
    guard(:Float, local.vrecv, local.snapshot) {
      guard(:Float, local.obj, local.vobj, local.snapshot) {
        guard("Float.!=") {
          local.v = emit :FloatNe, local.recv, local.obj
          push local.v
        }
      }
    }
    other {
      local.v = emit :CallMethod, local.recv, local.obj
      push local.v
    }
  }
  match(:opt_gt) {
    local.snapshot = take_snapshot
    local.obj  = pop
    local.recv = pop
    local.vobj  = topn(0)
    local.vrecv = topn(1)
    guard(:Fixnum, local.recv, local.vrecv, local.snapshot) {
      guard(:Fixnum, local.obj, local.vobj, local.snapshot) {
        guard("Fixnum.>") {
          local.v = emit :FixnumGt, local.recv, local.obj
          push local.v
        }
      }
    }
    guard(:Float, local.vrecv, local.snapshot) {
      guard(:Float, local.obj, local.vobj, local.snapshot) {
        guard("Float.>") {
          local.v = emit :FloatGt, local.recv, local.obj
          push local.v
        }
      }
    }
    other {
      local.v = emit :CallMethod, local.recv, local.obj
      push local.v
    }
  }
  match(:opt_ge) {
    local.snapshot = take_snapshot
    local.obj  = pop
    local.recv = pop
    local.vobj  = topn(0)
    local.vrecv = topn(1)
    guard(:Fixnum, local.recv, local.vrecv, local.snapshot) {
      guard(:Fixnum, local.obj, local.vobj, local.snapshot) {
        guard("Fixnum.>=") {
          local.v = emit :FixnumGe, local.recv, local.obj
          push local.v
        }
      }
    }
    guard(:Float, local.vrecv, local.snapshot) {
      guard(:Float, local.obj, local.vobj, local.snapshot) {
        guard("Float.>=") {
          local.v = emit :FloatGe, local.recv, local.obj
          push local.v
        }
      }
    }
    other {
      local.v = emit :CallMethod, local.recv, local.obj
      push local.v
    }
  }
  match(:opt_lt) {
    local.snapshot = take_snapshot
    local.obj  = pop
    local.recv = pop
    local.vobj  = topn(0)
    local.vrecv = topn(1)
    guard(:Fixnum, local.recv, local.vrecv, local.snapshot) {
      guard(:Fixnum, local.obj, local.vobj, local.snapshot) {
        guard("Fixnum.<") {
          local.v = emit :FixnumLt, local.recv, local.obj
          push local.v
        }
      }
    }
    guard(:Float, local.vrecv, local.snapshot) {
      guard(:Float, local.obj, local.vobj, local.snapshot) {
        guard("Float.<") {
          local.v = emit :FloatLt, local.recv, local.obj
          push local.v
        }
      }
    }
    other {
      local.v = emit :CallMethod, local.recv, local.obj
      push local.v
    }
  }
  match(:opt_le) {
    local.snapshot = take_snapshot
    local.obj  = pop
    local.recv = pop
    local.vobj  = topn(0)
    local.vrecv = topn(1)
    guard(:Fixnum, local.recv, local.vrecv, local.snapshot) {
      guard(:Fixnum, local.obj, local.vobj, local.snapshot) {
        guard("Fixnum.<=") {
          local.v = emit :FixnumLe, local.recv, local.obj
          push local.v
        }
      }
    }
    guard(:Float, local.vrecv, local.snapshot) {
      guard(:Float, local.obj, local.vobj, local.snapshot) {
        guard("Float.<=") {
          local.v = emit :FloatLe, local.recv, local.obj
          push local.v
        }
      }
    }
    other {
      local.v = emit :CallMethod, local.recv, local.obj
      push local.v
    }
  }
  match(:opt_ltlt) {
    local.snapshot = take_snapshot
    local.obj  = pop
    local.recv = pop
    local.vobj  = topn(0)
    local.vrecv = topn(1)
    guard(:String, local.vrecv, local.snapshot) {
      guard(:String, local.vobj, local.snapshot) {
        guard("String.<<") {
          local.v = emit :StringAdd, local.recv, local.obj
          push local.v
        }
      }
    }
    guard(:Array, local.vrecv, local.snapshot) {
      guard("Array.<<") {
        local.v = emit :ArrayAdd, local.recv, local.obj
        push local.v
      }
    }
    other {
      local.v = emit :CallMethod, local.recv, local.obj
      push local.v
    }
  }

  match(:opt_aref) {
    local.snapshot = take_snapshot
    local.obj  = pop
    local.recv = pop
    local.vobj  = topn(0)
    local.vrecv = topn(1)
    guard(:NonSpecialConst, local.vrecv, local.snapshot) {
      guard(:Array, local.vrecv, local.snapshot) {
        guard(:Fixnum, local.obj, local.vobj, local.snapshot) {
          guard("Array.[]") {
            local.v = emit :ArrayGet, local.recv, local.obj
            push local.v
          }
        }
      }
    }
    guard(:NonSpecialConst, local.vrecv, local.snapshot) {
      guard(:Hash, local.vrecv, local.snapshot) {
        guard("Hash.[]") {
          local.v = emit :HashGet, local.recv, local.obj
          push local.v
        }
      }
    }
    other {
      local.v = emit :CallMethod, local.recv, local.obj
      push local.v
    }
  }
  match(:opt_aref_with) {
    local.snapshot = take_snapshot
    local.recv = pop
    local.obj  = operand :VALUE, 2
    local.vrecv = topn(0)
    guard(:NonSpecialConst, local.vrecv, local.snapshot) {
      guard(:Array, local.vrecv, local.snapshot) {
        guard(:Fixnum, local.obj, local.vobj, local.snapshot) {
          guard("Array.[]") {
            local.v = emit :ArrayGet, local.recv, local.obj
            push local.v
          }
        }
      }
    }
    guard(:NonSpecialConst, local.vrecv, local.snapshot) {
      guard(:Hash, local.vrecv, local.snapshot) {
        guard("Hash.[]") {
          local.v = emit :HashGet, local.recv, local.obj
          push local.v
        }
      }
    }
    other {
      local.v = emit :CallMethod, local.recv, local.obj
      push local.v
    }
  }
  match(:opt_aset) {
    local.snapshot = take_snapshot
    local.obj  = pop
    local.recv = pop
    local.vobj  = topn(0)
    local.vrecv = topn(1)
    guard(:NonSpecialConst, local.vrecv, local.snapshot) {
      guard(:Array, local.vrecv, local.snapshot) {
        guard(:Fixnum, local.obj, local.vobj, local.snapshot) {
          guard("Array.[]=") {
            local.v = emit :ArraySet, local.recv, local.obj
            push local.v
          }
        }
      }
    }
    guard(:NonSpecialConst, local.vrecv, local.snapshot) {
      guard(:Hash, local.vrecv, local.snapshot) {
        guard("Hash.[]=") {
          local.v = emit :HashSet, local.recv, local.obj
          push local.v
        }
      }
    }
    other {
      local.v = emit :CallMethod, local.recv, local.obj
      push local.v
    }
  }
  match(:opt_aset_with) {
    local.snapshot = take_snapshot
    local.recv = pop
    local.obj  = operand :VALUE, 2
    local.vrecv = topn(0)
    guard(:NonSpecialConst, local.vrecv, local.snapshot) {
      guard(:Array, local.vrecv, local.snapshot) {
        guard(:Fixnum, local.obj, local.vobj, local.snapshot) {
          guard("Array.[]=") {
            local.v = emit :ArraySet, local.recv, local.obj
            push local.v
          }
        }
      }
    }
    guard(:NonSpecialConst, local.vrecv, local.snapshot) {
      guard(:Hash, local.vrecv, local.snapshot) {
        guard("Hash.[]=") {
          local.v = emit :HashSet, local.recv, local.obj
          push local.v
        }
      }
    }
    other {
      local.v = emit :CallMethod, local.recv, local.obj
      push local.v
    }
  }
  match(:opt_length) {
    local.snapshot = take_snapshot
    local.recv = pop
    local.vrecv = topn(0)
    guard(:String, local.vrecv, local.snapshot) {
      guard("String.length") {
        local.v = emit :StringLength, local.recv
        push local.v
      }
    }
    guard(:Array, local.vrecv, local.snapshot) {
      guard("Array.length") {
        local.v = emit :ArrayLength, local.recv
        push local.v
      }
    }
    guard(:Hash, local.vrecv, local.snapshot) {
      guard("Hash.length") {
        local.v = emit :HashLength, local.recv
        push local.v
      }
    }
    other {
      local.v = emit :CallMethod, local.recv
      push local.v
    }
  }
  match(:opt_size) {
    local.snapshot = take_snapshot
    local.recv = pop
    local.vrecv = topn(0)
    guard(:String, local.vrecv, local.snapshot) {
      guard("String.size") {
        local.v = emit :StringLength, local.recv
        push local.v
      }
    }
    guard(:Array, local.vrecv, local.snapshot) {
      guard("Array.size") {
        local.v = emit :ArrayLength, local.recv
        push local.v
      }
    }
    guard(:Hash, local.vrecv, local.snapshot) {
      guard("Hash.size") {
        local.v = emit :HashLength, local.recv
        push local.v
      }
    }
    other {
      local.v = emit :CallMethod, local.recv
      push local.v
    }
  }
  match(:opt_empty_p) {
    local.snapshot = take_snapshot
    local.recv = pop
    local.vrecv = topn(0)
    guard(:String, local.vrecv, local.snapshot) {
      guard("String.empty?") {
        local.v = emit :StringEmptyP, local.recv
        push local.v
      }
    }
    guard(:Array, local.vrecv, local.snapshot) {
      guard("Array.empty?") {
        local.v = emit :ArrayEmptyP, local.recv
        push local.v
      }
    }
    guard(:Hash, local.vrecv, local.snapshot) {
      guard("Hash.empty?") {
        local.v = emit :HashEmptyP, local.recv
        push local.v
      }
    }
    other {
      local.v = emit :CallMethod, local.recv
      push local.v
    }
  }

  # match(:opt_succ)
  #    ["FixnumSucc",       "opt_succ",        "succ", [:Fixnum]],
  #    ["TimeSucc",    "opt_succ", "succ", [:Time]],
  # match(:opt_not)
  # match(:opt_regexpmatch1)
  # match(:opt_regexpmatch2)
  match(:getlocal_OP__WC__0) {
    local.a = operand :int, 1
    local.v = emit :EnvLoad, local.a, 0
    push local.v
  }
  match(:getlocal_OP__WC__1) {
    local.a = operand :int, 1
    local.v = emit :EnvLoad, local.a, 1
    push local.v
  }
  match(:setlocal_OP__WC__0) {
    local.a = operand :int, 1
    local.v = pop
    local.result =emit :EnvStore, local.a, 0, local.v
  }
  match(:setlocal_OP__WC__1) {
    local.a = operand :int, 1
    local.v = pop
    local.result = emit :EnvStore, local.a, 1, local.v
  }
  # match(:putobject_OP_INT2FIX_O_0_C_)
  # match(:putobject_OP_INT2FIX_O_1_C_)

  #[
  #    ["ObjectNot", "opt_not", "!",  [:_]],
  #    ["ObjectNe",  "opt_neq", "!=", [:_, :_]],
  #    ["ObjectEq",  "opt_eq",  "==", [:_, :_]],
  #    ["RegExpMatch", "opt_regexpmatch1|opt_regexpmatch2", "=~", [:String, :Regexp]],
  #    ["ObjectToString", "tostring", "", [:String]],
  #];
end
