#include "compiler.h"

char ERR_INVALID_EXPRESSION[] = "Invalid Expression";


bool COMPILER::CompileExpression(SEGMENT_DESC &Segment)
{
    uint32_t dwVarCode;
    const VarInfo *real_var;
    LocalVarInfo lvi;

    const S_TOKEN_TYPE Token_type = CompileAuxiliaryTokens(Segment);
    if (Token_type == SEPARATOR)
        return false; // the failing operand reported the error

    if (Token_type == AND)
    {

        S_TOKEN_TYPE sttVariableType = Token.Get();

        if (sttVariableType != UNKNOWN)
        {
            SetError("invalid '&' usage");
            return false;
        }

        sttVariableType = DetectUnknown(dwVarCode);
        if (sttVariableType == CALL_FUNCTION)
        {
            // &Function: a function reference
            const std::string name = Token.GetData();
            const uint32_t size = static_cast<uint32_t>(name.size() + 1);
            CompileToken(Segment, STACK_PUSH);
            CompileToken(Segment, STRING, 2, &size, sizeof(uint32_t), name.c_str(), size);
            uint32_t target = VAR_FREF;
            CompileToken(Segment, OP_CAST, 1, &target, sizeof(uint32_t));
            CompileAuxiliaryTokens(Segment);
            return true;
        }
        if (!(sttVariableType == VARIABLE || sttVariableType == LOCAL_VARIABLE))
        {
            SetError("variable not found");
            return false;
        }

        const S_TOKEN_TYPE sttTokenType = Token.Get();
        if (sttTokenType == SQUARE_OPEN_BRACKET && IsArefVariable(sttVariableType, dwVarCode))
        {
            // &aref[i]: its i-th attribute
            if (!CompileExpression(Segment))
                return false;
            if (Token.GetType() != SQUARE_CLOSE_BRACKET)
            {
                SetError("missing ']'");
                return false;
            }
            CompileToken(Segment, STACK_POP);
            CompileToken(Segment, BX);
            CompileToken(Segment, MOVEAP);
            CompileToken(Segment, AP);
            CompileToken(Segment, sttVariableType, 1, &dwVarCode, sizeof(uint32_t));
            CompileToken(Segment, AP_CHILD);
            CompileToken(Segment, PUSH_AP);
            CompileAuxiliaryTokens(Segment);
            return true;
        }
        if (sttTokenType == DOT || (sttTokenType == SQUARE_OPEN_BRACKET && IsIndexedChain()))
            return CompileAttributeAddress(Segment, sttVariableType, dwVarCode, kAccessCreate);

        // alloc for result
        CompileToken(Segment, STACK_ALLOC);

        if (sttTokenType == SQUARE_OPEN_BRACKET)
        {
            if (sttVariableType == VARIABLE)
            {
                real_var = VarTab.GetVarX(dwVarCode);
                if (real_var == nullptr)
                {
                    SetError("Invalid var code");
                    return false;
                }

                // check for possibilities of '[' operator
                if (!real_var->IsArray())
                {
                    if (real_var->type != VAR_REFERENCE)
                    {
                        SetError("EN: %d", real_var->elements);
                        SetError(" A Invalid '[' operator, %s - isnt array", real_var->name.c_str());
                        return false;
                    }
                }
            }
            else
            {
                FuncTab.GetVar(lvi, CurrentFuncCode, dwVarCode);
                // check for possibilities of '[' operator
                if (!lvi.IsArray())
                {
                    if (lvi.type != VAR_REFERENCE)
                    {
                        SetError(" B Invalid '[' operator, %s - isnt array", lvi.name.c_str());
                        return false;
                    }
                }
            }

            // alloc for array index
            // after top of the stack must be array index
            Token.Get();
            if (!CompileExpression_L2(Segment))
            {
                SetError("invalid syntax");
                return false;
            }
            // test for ']'
            if (Token.GetType() != SQUARE_CLOSE_BRACKET)
            {
                SetError("missing ']'");
                return false;
            }

            CompileToken(Segment, STACK_POP);
            CompileToken(Segment, BX);

            CompileToken(Segment, SETREF_BXINDEX);
            CompileToken(Segment, STACK_TOP);
            CompileToken(Segment, sttVariableType, 1, &dwVarCode, sizeof(uint32_t));

            // next after ']'
            CompileAuxiliaryTokens(Segment);
        }
        else
        {
            CompileToken(Segment, SETREF);
            CompileToken(Segment, STACK_TOP);
            CompileToken(Segment, sttVariableType, 1, &dwVarCode, sizeof(uint32_t));
        }


        return true;
    }
    return CompileTernary(Segment);
}

// cond ? a : b
bool COMPILER::CompileTernary(SEGMENT_DESC &Segment)
{
    if (!CompileExpression_L0(Segment))
        return false;
    if (Token.GetType() != OP_QUESTION)
        return true;
    uint32_t jump = 0;
    CompileToken(Segment, STACK_POP);
    CompileToken(Segment, EX);
    const uint32_t to_else = Segment.CodeSize() + 2;
    CompileToken(Segment, JUMP_Z, 1, &jump, sizeof(uint32_t));
    if (!CompileExpression(Segment))
        return false;
    if (Token.GetType() != LABEL)
    {
        SetError("':' expected in conditional expression");
        return false;
    }
    const uint32_t to_end = Segment.CodeSize() + 2;
    CompileToken(Segment, JUMP, 1, &jump, sizeof(uint32_t));
    Segment.SetOffset(to_else, Segment.CodeSize());
    if (!CompileExpression(Segment))
        return false;
    Segment.SetOffset(to_end, Segment.CodeSize());
    return true;
}

// binary operators of one level: operand, then (op operand)*; each op is OP STACK_TOP AX
bool COMPILER::CompileBinaryLevel(SEGMENT_DESC &Segment, bool (COMPILER::*operand)(SEGMENT_DESC &),
                                  std::initializer_list<S_TOKEN_TYPE> ops, S_TOKEN_TYPE emitted_for_and)
{
    if (!(this->*operand)(Segment))
        return false;
    for (;;)
    {
        S_TOKEN_TYPE op = Token.GetType();
        if (std::find(ops.begin(), ops.end(), op) == ops.end())
            return true;
        CompileAuxiliaryTokens(Segment);
        if (!(this->*operand)(Segment))
            return false;
        CompileToken(Segment, STACK_POP);
        CompileToken(Segment, AX);
        CompileToken(Segment, op == AND ? emitted_for_and : op);
        CompileToken(Segment, STACK_TOP);
        CompileToken(Segment, AX);
    }
}

bool COMPILER::CompileBitOr(SEGMENT_DESC &Segment)
{
    return CompileBinaryLevel(Segment, &COMPILER::CompileBitAnd, {OP_BIT_OR}, OP_BIT_AND);
}

bool COMPILER::CompileBitAnd(SEGMENT_DESC &Segment)
{
    return CompileBinaryLevel(Segment, &COMPILER::CompileExpression_L1, {AND}, OP_BIT_AND);
}

bool COMPILER::CompileShift(SEGMENT_DESC &Segment)
{
    return CompileBinaryLevel(Segment, &COMPILER::CompileExpression_L2, {OP_SHL, OP_SHR}, OP_BIT_AND);
}

// logical operators (OR,AND)
bool COMPILER::CompileExpression_L0(SEGMENT_DESC &Segment)
{
    uint32_t dwJumpOffset1;
    uint32_t dwJumpOffset2;

    if (!CompileBitOr(Segment))
        return false; // the failing operand reported the error

    S_TOKEN_TYPE sttOpType = Token.GetType();

    while (sttOpType == OP_BOOL_AND || sttOpType == OP_BOOL_OR)
    {
        sttOpType = Token.GetType();

        CompileAuxiliaryTokens(Segment);

        // copy to EX value of first operand

        CompileToken(Segment, STACK_READ);
        CompileToken(Segment, EX);

        const uint32_t dwUpdateOffset1 = Segment.CodeSize() + 2;
        // point of jump not determined yet
        dwJumpOffset1 = 0;
        // compile jump command
        if (sttOpType == OP_BOOL_AND)
        {
            // AND : if 1st operand is zero - doesnt need to calculate second - jump to jump1
            CompileToken(Segment, JUMP_Z, 1, (char *)&dwJumpOffset1, sizeof(uint32_t));
        }
        else
        {
            // OR : if 1st operand is non zero - doesnt need to calculate second - jump to jump1
            CompileToken(Segment, JUMP_NZ, 1, (char *)&dwJumpOffset1, sizeof(uint32_t));
        }

        if (!CompileBitOr(Segment))
            return false;

        CompileToken(Segment, STACK_POP);
        CompileToken(Segment, AX);

        CompileToken(Segment, sttOpType);
        CompileToken(Segment, STACK_TOP);
        CompileToken(Segment, AX);

        // skip next section - proceed to jump2
        const uint32_t dwUpdateOffset2 = Segment.CodeSize() + 2;
        dwJumpOffset2 = 0;
        CompileToken(Segment, JUMP, 1, (char *)&dwJumpOffset2, sizeof(uint32_t));

        // jump1
        // just convert stack top to bool
        Segment.SetOffset(dwUpdateOffset1, Segment.CodeSize());
        CompileToken(Segment, OP_BOOL_CONVERT);
        CompileToken(Segment, STACK_TOP);

        // jump2
        // end of operation
        Segment.SetOffset(dwUpdateOffset2, Segment.CodeSize());

        sttOpType = Token.GetType();
    }
    return true;
}

// logical compare (>,<,==,>=,<=,!=)
bool COMPILER::CompileExpression_L1(SEGMENT_DESC &Segment)
{
    if (!CompileShift(Segment))
        return false; // the failing operand reported the error

    for (;;)
    {
        S_TOKEN_TYPE sttOpType = Token.GetType();
        const char *word = sttOpType == UNKNOWN && Token.GetData() ? Token.GetData() : "";
        if (sttOpType == OP_BOOL_NEG || storm::iEquals(word, "in") || storm::iEquals(word, "is"))
        {
            // "key" in object, "key" !in object, x is nullptr
            bool negate = sttOpType == OP_BOOL_NEG;
            if (negate)
            {
                Token.Get();
                if (!(Token.GetType() == UNKNOWN && storm::iEquals(Token.GetData(), "in")))
                {
                    SetError("'in' expected after '!'");
                    return false;
                }
                word = "in";
            }
            if (storm::iEquals(word, "is"))
            {
                Token.Get();
                if (!(Token.GetType() == UNKNOWN && storm::iEquals(Token.GetData(), "nullptr")))
                {
                    SetError("'nullptr' expected after 'is'");
                    return false;
                }
                CompileToken(Segment, OP_IS_NULL);
                CompileToken(Segment, STACK_TOP);
                CompileAuxiliaryTokens(Segment);
                continue;
            }
            CompileAuxiliaryTokens(Segment);
            if (!CompileAttributeRoot(Segment))
                return false;
            CompileToken(Segment, OP_IN);
            if (negate)
            {
                CompileToken(Segment, OP_BOOL_NEG);
                CompileToken(Segment, STACK_TOP);
            }
            continue;
        }
        if (!(sttOpType == OP_NOT_EQUAL || sttOpType == OP_LESSER_OR_EQUAL || sttOpType == OP_LESSER ||
              sttOpType == OP_GREATER_OR_EQUAL || sttOpType == OP_GREATER || sttOpType == OP_BOOL_EQUAL))
            return true;

        CompileAuxiliaryTokens(Segment);
        if ((sttOpType == OP_BOOL_EQUAL || sttOpType == OP_NOT_EQUAL) && Token.GetType() == UNKNOWN &&
            storm::iEquals(Token.GetData(), "nullptr"))
        {
            // x == nullptr: a null reference, attribute reference or function reference
            CompileToken(Segment, OP_IS_NULL);
            CompileToken(Segment, STACK_TOP);
            if (sttOpType == OP_NOT_EQUAL)
            {
                CompileToken(Segment, OP_BOOL_NEG);
                CompileToken(Segment, STACK_TOP);
            }
            CompileAuxiliaryTokens(Segment);
            continue;
        }

        if (!CompileShift(Segment))
            return false;

        CompileToken(Segment, STACK_POP);
        CompileToken(Segment, AX);
        CompileToken(Segment, sttOpType);
        CompileToken(Segment, STACK_TOP);
        CompileToken(Segment, AX);
    }
}

bool COMPILER::CompileExpression_L2(SEGMENT_DESC &Segment)
{
    if (!CompileExpression_L3(Segment))
        return false; // the failing operand reported the error

    S_TOKEN_TYPE sttOpType = Token.GetType();

    while (sttOpType == OP_PLUS || sttOpType == OP_MINUS)
    {
        sttOpType = Token.GetType();

        CompileAuxiliaryTokens(Segment);

        if (!CompileExpression_L3(Segment))
            return false;

        CompileToken(Segment, STACK_POP);
        CompileToken(Segment, AX);
        CompileToken(Segment, sttOpType);
        CompileToken(Segment, STACK_TOP);
        CompileToken(Segment, AX);

        sttOpType = Token.GetType();
    }
    return true;
}

// '*' and '/' operators
bool COMPILER::CompileExpression_L3(SEGMENT_DESC &Segment)
{
    if (!CompileExpression_L4(Segment))
        return false; // the failing operand reported the error

    S_TOKEN_TYPE sttOpType = Token.GetType();

    while (sttOpType == OP_MULTIPLY || sttOpType == OP_DIVIDE || sttOpType == OP_MODUL)
    {
        sttOpType = Token.GetType();
        CompileAuxiliaryTokens(Segment);

        if (!CompileExpression_L4(Segment))
            return false;

        CompileToken(Segment, STACK_POP);
        CompileToken(Segment, AX);
        CompileToken(Segment, sttOpType);
        CompileToken(Segment, STACK_TOP);
        CompileToken(Segment, AX);

        sttOpType = Token.GetType();
    }
    return true;
}

// '^' operations
bool COMPILER::CompileExpression_L4(SEGMENT_DESC &Segment)
{
    if (!CompileExpression_L5(Segment))
        return false; // the failing operand reported the error

    if (Token.GetType() == OP_POWER)
    {
        CompileAuxiliaryTokens(Segment);

        if (!CompileExpression_L5(Segment))
            return false;

        CompileToken(Segment, STACK_POP);
        CompileToken(Segment, AX);
        CompileToken(Segment, OP_POWER);
        CompileToken(Segment, STACK_TOP);
        CompileToken(Segment, AX);
    }
    return true;
}

// sign
bool COMPILER::CompileExpression_L5(SEGMENT_DESC &Segment)
{
    const S_TOKEN_TYPE sttOpType = Token.GetType();
    if (sttOpType == OP_DOLLAR)
    {
        // $(x): the text of x
        if (Token.Get() != OPEN_BRACKET)
        {
            SetError("'(' expected after '$'");
            return false;
        }
        if (!CompileExpression(Segment))
            return false;
        if (Token.GetType() != CLOSE_BRACKET)
        {
            SetError("')' expected after $(...");
            return false;
        }
        uint32_t target = VAR_STRING;
        CompileToken(Segment, OP_CAST, 1, &target, sizeof(uint32_t));
        if (CompileAuxiliaryTokens(Segment) == SQUARE_OPEN_BRACKET)
            return CompileSlice(Segment, STACK_TOP, 0);
        return true;
    }
    if (sttOpType == OP_SIZEOF)
    {
        // @array: number of elements; @object / @aref: number of attributes
        CompileAuxiliaryTokens(Segment);
        if (!CompileAttributeRoot(Segment))
            return false;
        CompileToken(Segment, OP_SIZEOF_REF);
        CompileToken(Segment, STACK_TOP);
        return true;
    }
    if (sttOpType == OP_PLUS || sttOpType == OP_MINUS || sttOpType == OP_BOOL_NEG || sttOpType == OP_BIT_NOT)
        CompileAuxiliaryTokens(Segment);

    const bool prefixed = sttOpType == OP_PLUS || sttOpType == OP_MINUS || sttOpType == OP_BOOL_NEG ||
                          sttOpType == OP_BIT_NOT;
    // the operand of a prefix operator may have one too (!@a, -~b)
    if (!(prefixed ? CompileExpression_L5(Segment) : CompileExpression_L6(Segment)))
        return false; // the failing operand reported the error

    switch (sttOpType)
    {
    case OP_MINUS:
        CompileToken(Segment, OP_SMINUS);
        CompileToken(Segment, STACK_TOP);
        break;
    case OP_BOOL_NEG:
        CompileToken(Segment, OP_BOOL_NEG);
        CompileToken(Segment, STACK_TOP);
        break;
    case OP_BIT_NOT:
        CompileToken(Segment, OP_BIT_NOT);
        CompileToken(Segment, STACK_TOP);
        break;
    }
    return true;
}

// '(' and ')'
bool COMPILER::CompileExpression_L6(SEGMENT_DESC &Segment)
{
    if (Token.GetType() == OPEN_BRACKET)
    {
        CompileAuxiliaryTokens(Segment);
        if (!CompileTernary(Segment))
            return false;
        if (Token.GetType() != CLOSE_BRACKET)
        {
            SetError("No matching ')' in expression");
            return false;
        }
        if (CompileAuxiliaryTokens(Segment) == SQUARE_OPEN_BRACKET)
            return CompileSlice(Segment, STACK_TOP, 0); // (expression)[a .. b]
        return true;
    }
    return CompileExpression_L7(Segment);
}

bool COMPILER::CompileExpression_L7(SEGMENT_DESC &Segment)
{
    uint32_t dwRCode = 0;
    S_TOKEN_TYPE sttResult;
    S_TOKEN_TYPE sttVariableField;
    S_TOKEN_TYPE sttFuncCallType;
    uint32_t dwVarCode;
    uint32_t dwAWCode;
    const VarInfo *real_var;
    LocalVarInfo lvi;
    FuncInfo fi;

    bool bDynamicCall = false;
    std::vector<uint32_t> chain; // offsets of the ADVANCE_AP instructions of an attribute chain
    switch (Token.GetType())
    {
    case SEPARATOR:

        return true;
    case VAR_INTEGER:
    case VAR_FLOAT:
    case VAR_STRING:
    case VAR_FREF:
    case VAR_AREFERENCE:
        if (Token.TypeCast())
            return CompileCast(Segment);
        break;
    case BLOCK_IN:
        return CompileObjectLiteral(Segment);
    case NUMBER:
        CompileToken(Segment, STACK_PUSH);
        CompileNumber(Segment);
        CompileAuxiliaryTokens(Segment);
        return true;
    case FLOAT_NUMBER:
        CompileToken(Segment, STACK_PUSH);
        CompileFloatNumber(Segment);
        CompileAuxiliaryTokens(Segment);
        return true;
    case STRING:
        CompileToken(Segment, STACK_PUSH);
        CompileString(Segment);
        CompileAuxiliaryTokens(Segment);
        return true;
    case TRUE_CONST:
        CompileToken(Segment, STACK_PUSH);
        dwRCode = 1;
        CompileToken(Segment, NUMBER, 1, (char *)&dwRCode, sizeof(uint32_t));
        CompileAuxiliaryTokens(Segment);
        return true;
    case FALSE_CONST:
        CompileToken(Segment, STACK_PUSH);
        dwRCode = 0;
        CompileToken(Segment, NUMBER, 1, (char *)&dwRCode, sizeof(uint32_t));
        CompileAuxiliaryTokens(Segment);
        return true;

    case CALL:
        bDynamicCall = true;
    case UNKNOWN:
        if (!bDynamicCall && Token.GetData() && storm::iEquals(Token.GetData(), "nullptr"))
        {
            CompileToken(Segment, PUSH_NULL);
            CompileAuxiliaryTokens(Segment);
            return true;
        }
        if (!bDynamicCall && Token.GetData() && storm::iEquals(Token.GetData(), "typeid"))
            return CompileTypeid(Segment);
        if (!bDynamicCall && Token.GetData() && storm::iEquals(Token.GetData(), "new"))
            return CompileNewArray(Segment);
        if (!bDynamicCall && Token.GetData() && IsAttributeIntrinsic(Token.GetData()))
            return CompileAttributeIntrinsic(Segment);
        if (!bDynamicCall)
            if (const S_TOKEN_TYPE call_type = DetectCallThrough(); call_type != UNKNOWN)
            {
                if (!CompileCallThrough(Segment, call_type))
                    return false;
                CompileAuxiliaryTokens(Segment);
                return true;
            }
        if (!bDynamicCall)
        {
            sttResult = DetectUnknown(dwRCode);
            // set func call method to standart
            sttFuncCallType = CALL_FUNCTION;
        }
        else
        {
            sttResult = CALL;
            sttFuncCallType = CALL_FUNCTION;
        }

        switch (sttResult)
        {
        case DEFINE_VAL:
            DEFINFO di;
            DefTab.GetDef(di, dwRCode);
            switch (di.deftype)
            {
            case NUMBER:
                CompileToken(Segment, STACK_PUSH);
                CompileToken(Segment, NUMBER, 1, (char *)&di.data4b, sizeof(uint32_t));
                CompileAuxiliaryTokens(Segment);
                break;
            case FLOAT_NUMBER:
                CompileToken(Segment, STACK_PUSH);
                CompileToken(Segment, FLOAT_NUMBER, 1, (char *)&di.data4b, sizeof(uint32_t));
                CompileAuxiliaryTokens(Segment);
                break;
            case STRING:
                CompileToken(Segment, STACK_PUSH);
                uint32_t string_size;
                string_size = strlen((char *)di.data4b) + 1;
                CompileToken(Segment, STRING, 2, (char *)&string_size, sizeof(uint32_t), (char *)di.data4b,
                             string_size);
                CompileAuxiliaryTokens(Segment);
                break;
            }
            return true;
        case CALL:
            // this is dynamic function call

            sttFuncCallType = CALL;

            if (Token.Get() != UNKNOWN)
            {
                SetError("invalid dynamic call syntax");
                return false;
            }

            sttVariableField = DetectUnknown(dwVarCode);

            if (sttVariableField == VARIABLE)
            {
                real_var = VarTab.GetVarX(dwVarCode);
                if (real_var == nullptr)
                {
                    SetError("Invalid var code");
                    return false;
                }

                if (real_var->type != VAR_STRING)
                {
                    SetError("'%s' must be string variable", Token.GetData());
                    return false;
                }
            }
            else if (sttVariableField == LOCAL_VARIABLE)
            {
                FuncTab.GetVar(lvi, CurrentFuncCode, dwVarCode);
                if (lvi.type != VAR_STRING)
                {
                    SetError("'%s' must be string variable", Token.GetData());
                    return false;
                }
            }
            else
            {
                SetError("invalid dynamic call syntax: '%s' must be string variable", Token.GetData());
                return false;
            }

            // proceed to standart func call routine

        case CALL_FUNCTION:
            // this is function call

            // first prepare stack for calling function

            if (Token.Get() != OPEN_BRACKET)
            {
                SetError("missing '('");
                return false;
            }

            // just get, already checked by DetectUnknown
            FuncTab.GetFuncX(fi, dwRCode);


            uint32_t func_args;
            if (!CompileCallArguments(Segment, func_args))
                return false;
            if (sttFuncCallType == CALL_FUNCTION && !CheckCallArguments(dwRCode, func_args, true))
                return false;


            if (sttFuncCallType == CALL_FUNCTION)
                CompileToken(Segment, CALL_FUNCTION, 1, (char *)&dwRCode, sizeof(uint32_t));
            else
            {
                // code of function must be find out on the fly from variable
                CompileToken(Segment, CALL);
                CompileToken(Segment, sttVariableField, 1, (char *)&dwVarCode, sizeof(uint32_t));
            }
            CompileToken(Segment, ARGS_NUM, 1, (char *)&func_args, sizeof(uint32_t));

            if (CompileAuxiliaryTokens(Segment) == DOT)
            {
                // f(x).name: an attribute of the object or attribute reference the call returned
                CompileToken(Segment, AP_FROM_STACK);
                CompileToken(Segment, STACK_ALLOC);
                return CompileChainTail(Segment);
            }

            return true;
        case LOCAL_VARIABLE:
        case VARIABLE:

            sttVariableField = sttResult;

            // variable code
            dwVarCode = dwRCode;

            // we going to place into stack variable value or attribute string - alloc stack data
            CompileToken(Segment, STACK_ALLOC);

            sttResult = Token.Get();
            if (sttResult == SQUARE_OPEN_BRACKET && (IsSlice() || IsTextVariable(sttVariableField, dwVarCode)))
                return CompileSlice(Segment, sttVariableField, dwVarCode); // text[a .. b], text[i]
            if (sttResult == SQUARE_OPEN_BRACKET && IsArefVariable(sttVariableField, dwVarCode))
            {
                // aref[i]: its i-th attribute
                if (!CompileExpression(Segment))
                    return false;
                if (Token.GetType() != SQUARE_CLOSE_BRACKET)
                {
                    SetError("missing ']'");
                    return false;
                }
                CompileToken(Segment, STACK_POP);
                CompileToken(Segment, BX);
                CompileToken(Segment, MOVEAP);
                CompileToken(Segment, AP);
                CompileToken(Segment, sttVariableField, 1, (char *)&dwVarCode, sizeof(uint32_t));
                CompileToken(Segment, AP_CHILD);
                if (Token.Get() == DOT)
                    return CompileChainTail(Segment); // aref[i].name
                CompileToken(Segment, STACK_POP_VOID); // the value slot: the result is the reference
                CompileToken(Segment, PUSH_AP);
                return true;
            }
            if (sttResult == SQUARE_OPEN_BRACKET)
            {
                // way for array element

                // doesnt need to check, becose already pass DetectUnknown function

                if (sttVariableField == VARIABLE)
                {
                    real_var = VarTab.GetVarX(dwVarCode);
                    if (real_var == nullptr)
                    {
                        SetError("Invalid var code");
                        return false;
                    }

                    // check for possibilities of '[' operator
                    if (!real_var->IsArray())
                    {
                        if (real_var->type != VAR_REFERENCE)
                        {
                            SetError(" C Invalid '[' operator, %s - isnt array", real_var->name.c_str());
                            return false;
                        }
                    }
                }
                else
                {
                    FuncTab.GetVar(lvi, CurrentFuncCode, dwVarCode);
                    // check for possibilities of '[' operator
                    if (!lvi.IsArray())
                    {
                        if (lvi.type != VAR_REFERENCE)
                        {
                            SetError(" D Invalid '[' operator, %s - isnt array", lvi.name.c_str());
                            return false;
                        }
                    }
                }

                // after top of the stack must be array index
                Token.Get();
                if (!CompileExpression_L2(Segment))
                    return false;
                // test for ']'
                if (Token.GetType() != SQUARE_CLOSE_BRACKET)
                {
                    SetError("missing ']'");
                    return false;
                }
                sttResult = CompileAuxiliaryTokens(Segment);

                // if not accessing attribute - copy value and return
                if (sttResult != DOT)
                {
                    // optimiztion possible: MOVE [STACK-1],[STACK]

                    CompileToken(Segment, STACK_POP);
                    CompileToken(Segment, BX);

                    // write to stack element value (index in X)
                    CompileToken(Segment, STACK_WRITE_BXINDEX);
                    CompileToken(Segment, sttVariableField, 1, (char *)&dwVarCode, sizeof(uint32_t));

                    // a[i][j]: an element of the array a[i] refers to
                    while (Token.GetType() == SQUARE_OPEN_BRACKET)
                    {
                        if (!CompileExpression(Segment))
                            return false;
                        if (Token.GetType() != SQUARE_CLOSE_BRACKET)
                        {
                            SetError("missing ']'");
                            return false;
                        }
                        CompileToken(Segment, STACK_POP);
                        CompileToken(Segment, BX);
                        CompileToken(Segment, STACK_INDEX);
                        CompileAuxiliaryTokens(Segment);
                    }

                    return true;
                }

                // in other case want to access attribute string value

                CompileToken(Segment, STACK_POP);
                CompileToken(Segment, BX);

                // move ap to AP register
                CompileToken(Segment, MOVEAP_BXINDEX);
                CompileToken(Segment, AP);
                CompileToken(Segment, sttVariableField, 1, (char *)&dwVarCode, sizeof(uint32_t));

            }
            else
            {
                // way for non array var

                if (sttResult == OP_DOLLAR)
                {
                    CompileToken(Segment, STACK_POP_VOID);
                    CompileToken(Segment, MOVEAP);
                    CompileToken(Segment, AP);
                    CompileToken(Segment, sttVariableField, 1, (char *)&dwVarCode, sizeof(uint32_t));
                    return CompileAttributeDefault(Segment);
                }
                if (sttResult != DOT)
                {
                    CompileToken(Segment, STACK_WRITE);
                    CompileToken(Segment, sttVariableField, 1, (char *)&dwVarCode, sizeof(uint32_t));
                    return true;
                }

                // in other case want to access attribute string value

                // move ap to AP register
                CompileToken(Segment, MOVEAP);
                CompileToken(Segment, AP);
                CompileToken(Segment, sttVariableField, 1, (char *)&dwVarCode, sizeof(uint32_t));
            }

            return CompileChainTail(Segment);
        default:
            SetError("unexpected '%s' in expression", Token.GetData() ? Token.GetData() : "");
            return false;
        }
        return true;
    }
    SetError("unexpected '%s' in expression", Token.GetData() ? Token.GetData() : Token.GetTypeName(Token.GetType()));
    return false;
}

// int(x), float(x), bool(x), string(x), kstring(x), fref(x)
bool COMPILER::CompileCast(SEGMENT_DESC &Segment)
{
    const std::string keyword = Token.GetData() ? Token.GetData() : "";
    uint32_t target = Token.GetType();
    if (storm::iEquals(keyword, "bool"))
        target = TRUE_CONST; // marker: truth value
    if (Token.Get() != OPEN_BRACKET)
    {
        SetError("'(' expected after %s", keyword.c_str());
        return false;
    }
    if (!CompileExpression(Segment))
        return false;
    if (Token.GetType() != CLOSE_BRACKET)
    {
        SetError("')' expected after %s(...", keyword.c_str());
        return false;
    }
    CompileToken(Segment, OP_CAST, 1, &target, sizeof(uint32_t));
    if (CompileAuxiliaryTokens(Segment) == SQUARE_OPEN_BRACKET && target == VAR_STRING)
        return CompileSlice(Segment, STACK_TOP, 0); // string(x)[i]
    return true;
}

// typeid(type) is a constant, typeid(expression) the type of its value (through references)
bool COMPILER::CompileTypeid(SEGMENT_DESC &Segment)
{
    if (Token.Get() != OPEN_BRACKET)
    {
        SetError("'(' expected after typeid");
        return false;
    }
    const S_TOKEN_TYPE type = Token.Get();
    const char *data = Token.GetData() ? Token.GetData() : "";
    int32_t id = -1;
    switch (type)
    {
    case VAR_INTEGER:
        id = storm::iEquals(data, "bool") ? kTypeidBool : kTypeidInt;
        break;
    case VAR_FLOAT:
        id = kTypeidFloat;
        break;
    case VAR_STRING:
        id = kTypeidString;
        break;
    case VAR_OBJECT:
        id = kTypeidObject;
        break;
    case VAR_REFERENCE:
        id = kTypeidRef;
        break;
    case VAR_AREFERENCE:
        id = kTypeidAref;
        break;
    case VAR_FREF:
        id = kTypeidFref;
        break;
    default:
        break;
    }
    if (id >= 0)
    {
        if (Token.Get() != CLOSE_BRACKET)
        {
            SetError("')' expected after typeid(type");
            return false;
        }
        CompileToken(Segment, STACK_PUSH);
        CompileToken(Segment, NUMBER, 1, &id, sizeof(int32_t));
        CompileAuxiliaryTokens(Segment);
        return true;
    }
    Token.StepBack();
    if (!CompileExpression(Segment))
        return false;
    if (Token.GetType() != CLOSE_BRACKET)
    {
        SetError("')' expected after typeid(...");
        return false;
    }
    CompileToken(Segment, OP_TYPEID);
    CompileToken(Segment, STACK_TOP);
    CompileAuxiliaryTokens(Segment);
    return true;
}

// after the attribute chain (rAP set by MOVEAP/ADVANCE_AP_QUIET), on '$': type(default)
bool COMPILER::CompileAttributeDefault(SEGMENT_DESC &Segment)
{
    CompileToken(Segment, PUSH_AP);
    Token.Get();
    if (Token.GetType() == VAR_AREFERENCE && !Token.TypeCast())
    {
        // a.b$aref: the attribute reference itself (null when missing)
        CompileAuxiliaryTokens(Segment);
        return true;
    }
    const char *keyword = Token.GetData() ? Token.GetData() : "";
    uint32_t target = Token.GetType();
    if (!Token.TypeCast() ||
        !(target == VAR_INTEGER || target == VAR_FLOAT || target == VAR_STRING || target == VAR_FREF))
    {
        SetError("type(default) expected after '$'");
        return false;
    }
    if (storm::iEquals(keyword, "bool"))
        target = TRUE_CONST;
    Token.Get(); // '('
    if (!CompileExpression(Segment))
        return false;
    if (Token.GetType() != CLOSE_BRACKET)
    {
        SetError("')' expected after the default value");
        return false;
    }
    CompileToken(Segment, ATTR_DEFAULT, 1, &target, sizeof(uint32_t));
    CompileAuxiliaryTokens(Segment);
    return true;
}

// the operand of in and @: a variable, optionally indexed, and an optional attribute chain. Leaves a
// reference to the variable (no chain) or an attribute reference (null when an attribute is missing).
bool COMPILER::CompileAttributeRoot(SEGMENT_DESC &Segment)
{
    uint32_t var_code;
    if (Token.GetType() == AND)
        Token.Get(); // "key" in &object: the same operand
    if (Token.GetType() != UNKNOWN)
    {
        SetError("variable expected");
        return false;
    }
    const S_TOKEN_TYPE var_type = DetectUnknown(var_code);
    if (!(var_type == VARIABLE || var_type == LOCAL_VARIABLE))
    {
        SetError("variable expected, not '%s'", Token.GetData());
        return false;
    }
    bool indexed = false;
    if (CompileAuxiliaryTokens(Segment) == SQUARE_OPEN_BRACKET)
    {
        if (!CompileExpression(Segment))
            return false;
        if (Token.GetType() != SQUARE_CLOSE_BRACKET)
        {
            SetError("missing ']'");
            return false;
        }
        CompileToken(Segment, STACK_POP);
        CompileToken(Segment, BX);
        indexed = true;
        CompileAuxiliaryTokens(Segment);
    }
    if (Token.GetType() != DOT)
    {
        CompileToken(Segment, STACK_ALLOC);
        CompileToken(Segment, indexed ? SETREF_BXINDEX : SETREF);
        CompileToken(Segment, STACK_TOP);
        CompileToken(Segment, var_type, 1, &var_code, sizeof(uint32_t));
        return true;
    }
    CompileToken(Segment, indexed ? MOVEAP_BXINDEX : MOVEAP);
    CompileToken(Segment, AP);
    CompileToken(Segment, var_type, 1, &var_code, sizeof(uint32_t));
    while (Token.GetType() == DOT)
    {
        Token.Get();
        if (Token.GetType() == OPEN_BRACKET)
        {
            if (!CompileAccessExpression(Segment, kAccessQuiet))
                return false;
        }
        else
        {
            if (Token.GetData() == nullptr)
            {
                SetError("Invalid access string");
                return false;
            }
            Token.LowCase();
            const uint32_t word = SCodec.Convert(Token.GetData());
            CompileToken(Segment, ADVANCE_AP_QUIET);
            CompileToken(Segment, ACCESS_WORD_CODE, 1, &word, sizeof(uint32_t));
        }
        CompileAuxiliaryTokens(Segment);
    }
    CompileToken(Segment, PUSH_AP);
    return true;
}

// On an identifier: CALL_METHOD for "object->Method(", CALL for "fref_variable(", else UNKNOWN.
// The token stream is left where it was.
S_TOKEN_TYPE COMPILER::DetectCallThrough()
{
    uint32_t code;
    const S_TOKEN_TYPE var_type = DetectUnknown(code);
    if (!(var_type == VARIABLE || var_type == LOCAL_VARIABLE))
        return UNKNOWN;
    S_TOKEN_TYPE result = UNKNOWN;
    const S_TOKEN_TYPE next = Token.Get();
    if (next == OP_ARROW)
        result = CALL_METHOD;
    else if (next == OPEN_BRACKET)
    {
        S_TOKEN_TYPE type = UNKNOWN;
        if (var_type == VARIABLE)
        {
            if (const auto *var = VarTab.GetVarX(code))
                type = var->type;
        }
        else
        {
            LocalVarInfo lvi;
            if (FuncTab.GetVar(lvi, CurrentFuncCode, code))
                type = lvi.type;
        }
        if (type == VAR_FREF)
            result = CALL;
    }
    Token.StepBack(); // back to the identifier: its data again
    Token.StepBack();
    Token.Get();
    return result;
}

// object->Method(args): calls the function its class table names for Method with the object as
// the first argument; fref_variable(args): calls the function the reference names
bool COMPILER::CompileCallThrough(SEGMENT_DESC &Segment, S_TOKEN_TYPE call_type)
{
    uint32_t var_code;
    const S_TOKEN_TYPE var_type = DetectUnknown(var_code);
    uint32_t func_args = 0;
    if (call_type == CALL)
    {
        Token.Get(); // '('
        if (!CompileCallArguments(Segment, func_args))
            return false;
        CompileToken(Segment, CALL);
        CompileToken(Segment, var_type, 1, &var_code, sizeof(uint32_t));
        CompileToken(Segment, ARGS_NUM, 1, &func_args, sizeof(uint32_t));
        return true;
    }
    Token.Get(); // '->'
    if (Token.Get() != UNKNOWN || Token.GetData() == nullptr)
    {
        SetError("method name expected after '->'");
        return false;
    }
    const std::string method = Token.GetData();
    if (Token.Get() != OPEN_BRACKET)
    {
        SetError("'(' expected after '->%s'", method.c_str());
        return false;
    }
    // the object, by reference, is the first argument
    CompileToken(Segment, STACK_ALLOC);
    CompileToken(Segment, SETREF);
    CompileToken(Segment, STACK_TOP);
    CompileToken(Segment, var_type, 1, &var_code, sizeof(uint32_t));
    if (!CompileCallArguments(Segment, func_args))
        return false;
    func_args++;
    const uint32_t size = static_cast<uint32_t>(method.size() + 1);
    CompileToken(Segment, CALL_METHOD, 2, &size, sizeof(uint32_t), method.c_str(), size);
    CompileToken(Segment, ARGS_NUM, 1, &func_args, sizeof(uint32_t));
    return true;
}

// { key = value, "key" = value, classtable = GlobalObject }: a new object with these attributes
bool COMPILER::CompileObjectLiteral(SEGMENT_DESC &Segment)
{
    {
        // "{ a, b }" is an array literal: its first item is not "key ="
        char *const at = Token.GetProgramControl();
        const uint32_t line = DebugSourceLine;
        S_TOKEN_TYPE first = Token.Get();
        while (first == DEBUG_LINEFEED || first == COMMENT)
            first = Token.Get();
        const bool object = first == BLOCK_OUT || ((first == UNKNOWN || first == STRING) && Token.Get() == OP_EQUAL);
        Token.SetProgramControl(at);
        DebugSourceLine = line;
        if (!object)
            return CompileArrayLiteral(Segment);
    }
    CompileToken(Segment, OBJ_NEW);
    for (;;)
    {
        S_TOKEN_TYPE t = CompileAuxiliaryTokens(Segment);
        if (t == BLOCK_OUT)
            break;
        if (!((t == UNKNOWN || t == STRING) && Token.GetData()))
        {
            SetError("attribute name expected in object literal");
            return false;
        }
        std::string key = Token.GetData();
        if (CompileAuxiliaryTokens(Segment) != OP_EQUAL)
        {
            SetError("'=' expected after '%s' in object literal", key.c_str());
            return false;
        }
        if (storm::iEquals(key, "classtable"))
        {
            // the class table is a global object, stored by name (survives saving)
            uint32_t var_code;
            if (CompileAuxiliaryTokens(Segment) != UNKNOWN || DetectUnknown(var_code) != VARIABLE)
            {
                SetError("classtable must name a global object");
                return false;
            }
            const std::string table = Token.GetData();
            const uint32_t size = static_cast<uint32_t>(table.size() + 1);
            CompileToken(Segment, STACK_PUSH);
            CompileToken(Segment, STRING, 2, &size, sizeof(uint32_t), table.c_str(), size);
            CompileAuxiliaryTokens(Segment);
        }
        else if (!CompileExpression(Segment))
            return false;
        for (auto &c : key)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const uint32_t size = static_cast<uint32_t>(key.size() + 1);
        CompileToken(Segment, OBJ_SET, 2, &size, sizeof(uint32_t), key.c_str(), size);
        if (Token.GetType() == BLOCK_OUT)
            break;
        if (Token.GetType() != COMMA)
        {
            SetError("',' or '}' expected in object literal");
            return false;
        }
    }
    CompileAuxiliaryTokens(Segment);
    return true;
}

// after '[': is this "[a .. b]" (a slice of text, either bound optional)? The position is kept.
bool COMPILER::IsSlice()
{
    char *const at = Token.GetProgramControl();
    bool slice = false;
    S_TOKEN_TYPE previous = SQUARE_OPEN_BRACKET;
    for (uint32_t depth = 0;;)
    {
        const S_TOKEN_TYPE t = Token.Get();
        if (t == END_OF_PROGRAMM || t == SEPARATOR || t == BLOCK_IN)
            break;
        if (t == SQUARE_OPEN_BRACKET || t == OPEN_BRACKET)
            depth++;
        else if (t == SQUARE_CLOSE_BRACKET || t == CLOSE_BRACKET)
        {
            if (depth-- == 0)
                break;
        }
        else if (t == DOT && previous == DOT && depth == 0)
        {
            slice = true;
            break;
        }
        if (t != DEBUG_LINEFEED && t != COMMENT)
            previous = t;
    }
    Token.SetProgramControl(at);
    return slice;
}

// [a .. b] after a variable (source) or after a bracketed expression (source STACK_TOP: the value on
// the stack); negative bounds count from the end; leaves the text slice on the stack
bool COMPILER::CompileSlice(SEGMENT_DESC &Segment, S_TOKEN_TYPE source, uint32_t source_code)
{
    uint32_t flags = 0;
    if (Token.Get() != DOT)
    {
        Token.StepBack();
        if (!CompileExpression(Segment))
            return false;
        flags |= 1;
        if (Token.GetType() == SQUARE_CLOSE_BRACKET)
        {
            // text[i]: one character
            flags |= 4;
            CompileToken(Segment, OP_SLICE, 1, &flags, sizeof(uint32_t));
            if (source == STACK_TOP)
                CompileToken(Segment, STACK_TOP);
            else
                CompileToken(Segment, source, 1, &source_code, sizeof(uint32_t));
            CompileAuxiliaryTokens(Segment);
            return true;
        }
    }
    if (Token.GetType() != DOT || Token.Get() != DOT)
    {
        SetError("'..' expected in slice");
        return false;
    }
    if (Token.Get() != SQUARE_CLOSE_BRACKET)
    {
        Token.StepBack();
        if (!CompileExpression(Segment))
            return false;
        if (Token.GetType() != SQUARE_CLOSE_BRACKET)
        {
            SetError("']' expected after slice");
            return false;
        }
        flags |= 2;
    }
    CompileToken(Segment, OP_SLICE, 1, &flags, sizeof(uint32_t));
    if (source == STACK_TOP)
        CompileToken(Segment, STACK_TOP);
    else
        CompileToken(Segment, source, 1, &source_code, sizeof(uint32_t));
    CompileAuxiliaryTokens(Segment);
    return true;
}

// { a, b, c }: an array of the items' type
bool COMPILER::CompileArrayLiteral(SEGMENT_DESC &Segment)
{
    uint32_t count = 0;
    for (;;)
    {
        if (!CompileExpression(Segment))
            return false;
        count++;
        if (Token.GetType() == BLOCK_OUT)
            break;
        if (Token.GetType() != COMMA)
        {
            SetError("',' or '}' expected in array literal");
            return false;
        }
    }
    CompileToken(Segment, ARRAY_BUILD, 1, &count, sizeof(uint32_t));
    CompileAuxiliaryTokens(Segment);
    return true;
}

// after '(' of an attribute access: "(name)" with a variable compiles as ACCESS_VAR / ADVANCE_AP var,
// anything else is an expression (the position is kept)
bool COMPILER::IsAccessVariable()
{
    char *const at = Token.GetProgramControl();
    uint32_t code;
    const bool variable = Token.Get() == UNKNOWN &&
                          (DetectUnknown(code) == VARIABLE || DetectUnknown(code) == LOCAL_VARIABLE) &&
                          Token.Get() == CLOSE_BRACKET;
    Token.SetProgramControl(at - 1); // back on the '(' (one character) so that it is the current token
    Token.Get();
    return variable;
}

// after '[' following a variable: does "x[i]." (an attribute chain of an element) follow? Kept position.
bool COMPILER::IsIndexedChain()
{
    char *const at = Token.GetProgramControl();
    bool chain = false;
    for (uint32_t depth = 0;;)
    {
        const S_TOKEN_TYPE t = Token.Get();
        if (t == END_OF_PROGRAMM || t == SEPARATOR || t == BLOCK_IN)
            break;
        if (t == SQUARE_OPEN_BRACKET)
            depth++;
        else if (t == SQUARE_CLOSE_BRACKET && depth-- == 0)
        {
            chain = Token.Get() == DOT;
            break;
        }
    }
    Token.SetProgramControl(at);
    return chain;
}

// on '(' of ".(expression)" in an rvalue chain: rAP saved on the stack while the expression runs
bool COMPILER::CompileAccessExpression(SEGMENT_DESC &Segment, uint32_t mode)
{
    CompileToken(Segment, PUSH_AP);
    if (!CompileExpression(Segment))
        return false;
    if (Token.GetType() != CLOSE_BRACKET)
    {
        SetError("missing ')' after attribute expression");
        return false;
    }
    CompileToken(Segment, ADVANCE_AP_EXPR, 1, &mode, sizeof(uint32_t));
    return true;
}

// a chain element compiled for reading becomes quiet (a missing attribute is null, not an error)
void COMPILER::QuietChainElement(SEGMENT_DESC &Segment, uint32_t offset)
{
    if (static_cast<uint8_t>(Segment.code[offset]) == ADVANCE_AP)
    {
        Segment.code[offset] = ADVANCE_AP_QUIET;
        return;
    }
    // PUSH_AP (2 bytes), the expression, ADVANCE_AP_EXPR <mode>: find that instruction
    for (uint32_t ip = offset; ip < Segment.CodeSize();)
    {
        const auto type = static_cast<uint8_t>(Segment.code[ip]);
        uint32_t size = static_cast<uint8_t>(Segment.code[ip + 1]);
        uint32_t data = ip + 2;
        if (size == 0xff)
        {
            memcpy(&size, &Segment.code[ip + 2], sizeof(uint32_t));
            data += sizeof(uint32_t);
        }
        if (type == ADVANCE_AP_EXPR)
        {
            const uint32_t quiet = kAccessQuiet;
            memcpy(&Segment.code[data], &quiet, sizeof(uint32_t));
            return;
        }
        ip = data + size;
    }
}

// &variable[index].chain: an attribute reference; mode kAccessCreate creates missing attributes
bool COMPILER::CompileAttributeAddress(SEGMENT_DESC &Segment, S_TOKEN_TYPE var_type, uint32_t var_code, uint32_t mode)
{
    if (Token.GetType() == SQUARE_OPEN_BRACKET)
    {
        if (!CompileExpression(Segment))
            return false;
        if (Token.GetType() != SQUARE_CLOSE_BRACKET)
        {
            SetError("missing ']'");
            return false;
        }
        CompileToken(Segment, STACK_POP);
        CompileToken(Segment, BX);
        CompileToken(Segment, MOVEAP_BXINDEX);
        Token.Get();
    }
    else
        CompileToken(Segment, MOVEAP);
    CompileToken(Segment, AP);
    CompileToken(Segment, var_type, 1, &var_code, sizeof(uint32_t));
    while (Token.GetType() == DOT)
    {
        Token.Get();
        if (Token.GetType() == OPEN_BRACKET)
        {
            if (!CompileAccessExpression(Segment, mode))
                return false;
        }
        else
        {
            if (Token.GetData() == nullptr)
            {
                SetError("Invalid access string");
                return false;
            }
            Token.LowCase();
            const uint32_t word = SCodec.Convert(Token.GetData());
            CompileToken(Segment, mode == kAccessCreate ? VERIFY_AP : ADVANCE_AP_QUIET);
            CompileToken(Segment, ACCESS_WORD_CODE, 1, &word, sizeof(uint32_t));
        }
        CompileAuxiliaryTokens(Segment);
    }
    CompileToken(Segment, PUSH_AP);
    return true;
}

bool COMPILER::IsAttributeIntrinsic(const char *name)
{
    return storm::iEquals(name, "touchattr") || storm::iEquals(name, "nameattr") ||
           storm::iEquals(name, "parentattr") || storm::iEquals(name, "deleteattr");
}

// touchattr(a.b) the attribute reference, creating missing attributes; nameattr(r) its name;
// parentattr(r) the attribute it belongs to; deleteattr(&a.b) removes it
bool COMPILER::CompileAttributeIntrinsic(SEGMENT_DESC &Segment)
{
    const std::string name = Token.GetData();
    if (Token.Get() != OPEN_BRACKET)
    {
        SetError("'(' expected after %s", name.c_str());
        return false;
    }
    if (storm::iEquals(name, "touchattr"))
    {
        uint32_t var_code;
        S_TOKEN_TYPE t = Token.Get();
        if (t == AND)
            t = Token.Get();
        const S_TOKEN_TYPE var_type = t == UNKNOWN ? DetectUnknown(var_code) : UNKNOWN;
        if (!(var_type == VARIABLE || var_type == LOCAL_VARIABLE))
        {
            SetError("touchattr needs a variable and an attribute path");
            return false;
        }
        Token.Get();
        if (!CompileAttributeAddress(Segment, var_type, var_code, kAccessCreate))
            return false;
    }
    else
    {
        if (!CompileExpression(Segment))
            return false;
        if (storm::iEquals(name, "nameattr"))
        {
            const uint32_t func_code = FuncTab.FindFunc("GetAttributeName");
            const uint32_t args = 1;
            CompileToken(Segment, CALL_FUNCTION, 1, &func_code, sizeof(uint32_t));
            CompileToken(Segment, ARGS_NUM, 1, &args, sizeof(uint32_t));
        }
        else
            CompileToken(Segment, storm::iEquals(name, "parentattr") ? ATTR_PARENT : ATTR_DELETE);
    }
    if (Token.GetType() != CLOSE_BRACKET)
    {
        SetError("')' expected after %s(...", name.c_str());
        return false;
    }
    CompileAuxiliaryTokens(Segment);
    return true;
}

bool COMPILER::IsArefVariable(S_TOKEN_TYPE var_type, uint32_t var_code)
{
    if (var_type == VARIABLE)
    {
        const auto *var = VarTab.GetVarX(var_code);
        return var && (var->type == VAR_AREFERENCE || var->type == VAR_OBJECT) && !var->IsArray();
    }
    LocalVarInfo lvi;
    return FuncTab.GetVar(lvi, CurrentFuncCode, var_code) &&
           (lvi.type == VAR_AREFERENCE || lvi.type == VAR_OBJECT) && !lvi.IsArray();
}

// after the root of an attribute chain (rAP set, a value slot allocated, current token '.'):
// the rest of the chain, then the attribute's text, a.b$type(default) or a call a.b(args)
bool COMPILER::CompileChainTail(SEGMENT_DESC &Segment)
{
    S_TOKEN_TYPE sttResult;
    uint32_t dwVarCode;
    uint32_t dwAWCode;
    std::vector<uint32_t> chain; // offsets of the ADVANCE_AP instructions
    sttResult = Token.Get();
    do
    {
        // if(sttResult == OPEN_BRACKET)
        if (Token.GetType() == OPEN_BRACKET && !IsAccessVariable())
        {
            // .(expression): the attribute named by the expression's text
            chain.push_back(Segment.CodeSize());
            if (!CompileAccessExpression(Segment, kAccessRead))
                return false;
        }
        else if (Token.GetType() == OPEN_BRACKET)
        {
            // access var
            sttResult = Token.Get();
            if (sttResult != UNKNOWN)
            {
                SetError("Invalid access var syntax");
                return false;
            }
            sttResult = DetectUnknown(dwVarCode);
            if (!(sttResult == VARIABLE || sttResult == LOCAL_VARIABLE))
            {
                SetError("not variable: %s", Token.GetData());
                return false;
            }
            chain.push_back(Segment.CodeSize());
            CompileToken(Segment, ADVANCE_AP);
            CompileToken(Segment, sttResult, 1, &dwVarCode, sizeof(uint32_t));
            if (Token.Get() != CLOSE_BRACKET)
            {
                SetError("missing ')'");
                return false;
            }
        }
        else
        {
            // access word
            if (Token.GetData() == nullptr)
            {
                SetError("Invalid access string");
                return false;
            }
            Token.LowCase();
            dwAWCode = SCodec.Convert(Token.GetData());
            chain.push_back(Segment.CodeSize());
            CompileToken(Segment, ADVANCE_AP);
            CompileToken(Segment, ACCESS_WORD_CODE, 1, &dwAWCode, sizeof(uint32_t));
        }
        sttResult = Token.Get();
        if (sttResult == DOT)
            Token.Get(); // get next token (after dot)
    } while (sttResult == DOT);

    if (Token.GetType() == OP_DOLLAR)
    {
        // a.b$int(0): the attribute converted to the type, or the default when it is missing
        for (const auto offset : chain)
            QuietChainElement(Segment, offset);
        CompileToken(Segment, STACK_POP_VOID); // the slot allocated for the value
        return CompileAttributeDefault(Segment);
    }

    if (Token.GetType() == OPEN_BRACKET)
    {
        // a.b(args): calls the function the attribute names (a function reference)
        CompileToken(Segment, STACK_WRITE);
        CompileToken(Segment, AP_VALUE);
        uint32_t func_args;
        if (!CompileCallArguments(Segment, func_args))
            return false;
        CompileToken(Segment, CALL_NAMED);
        CompileToken(Segment, ARGS_NUM, 1, &func_args, sizeof(uint32_t));
        if (CompileAuxiliaryTokens(Segment) == DOT)
        {
            CompileToken(Segment, AP_FROM_STACK); // a.f(x).name
            CompileToken(Segment, STACK_ALLOC);
            return CompileChainTail(Segment);
        }
        return true;
    }

    // write attribute string to stack top
    CompileToken(Segment, STACK_WRITE);
    CompileToken(Segment, AP_VALUE);

    return true;
}

// on an identifier starting a statement: is it "name[i].chain(args)"? (the position is kept)
bool COMPILER::IsAttributeCall()
{
    uint32_t code;
    const S_TOKEN_TYPE var_type = DetectUnknown(code);
    if (!(var_type == VARIABLE || var_type == LOCAL_VARIABLE))
        return false;
    char *const at = Token.GetProgramControl();
    const std::string name = Token.GetData();
    bool call = false, word = false, dot = false;
    auto skip = [this](S_TOKEN_TYPE open, S_TOKEN_TYPE close) {
        for (uint32_t depth = 0;;)
        {
            const S_TOKEN_TYPE t = Token.Get();
            if (t == END_OF_PROGRAMM || t == SEPARATOR)
                return false;
            if (t == open)
                depth++;
            else if (t == close && depth-- == 0)
                return true;
        }
    };
    for (bool more = true; more;)
    {
        const S_TOKEN_TYPE t = Token.Get();
        switch (t)
        {
        case DOT:
            dot = true;
            word = false;
            break;
        case SQUARE_OPEN_BRACKET:
            more = skip(SQUARE_OPEN_BRACKET, SQUARE_CLOSE_BRACKET);
            word = false;
            break;
        case OPEN_BRACKET:
            if (dot && word)
            {
                call = true;
                more = false;
            }
            else
                more = dot && skip(OPEN_BRACKET, CLOSE_BRACKET); // .(expression)
            word = false;
            break;
        case DEBUG_LINEFEED:
        case COMMENT:
            break;
        default:
            if (dot && !word && IsIdentifier(Token.GetData()))
                word = true; // an attribute name
            else
                more = false; // "a.b := f(x)" is an assignment, not a call
            break;
        }
    }
    Token.SetProgramControl(at - name.size()); // back on the identifier (current token again)
    Token.Get();
    return call;
}

bool COMPILER::IsTextVariable(S_TOKEN_TYPE var_type, uint32_t var_code)
{
    if (var_type == VARIABLE)
    {
        const auto *var = VarTab.GetVarX(var_code);
        return var && var->type == VAR_STRING && !var->IsArray();
    }
    LocalVarInfo lvi;
    return FuncTab.GetVar(lvi, CurrentFuncCode, var_code) && lvi.type == VAR_STRING && !lvi.IsArray();
}

// new type[size]: a reference to a new array (kept by the compiler until it is released)
bool COMPILER::CompileNewArray(SEGMENT_DESC &Segment)
{
    const S_TOKEN_TYPE type = Token.Get();
    if (!(type == VAR_INTEGER || type == VAR_FLOAT || type == VAR_STRING || type == VAR_OBJECT ||
          type == VAR_REFERENCE || type == VAR_AREFERENCE))
    {
        SetError("type expected after 'new'");
        return false;
    }
    // "string[16]": the lexer may have read '[' as the type's data
    if (!(Token.GetData() && strcmp(Token.GetData(), "[") == 0) && Token.Get() != SQUARE_OPEN_BRACKET)
    {
        SetError("'[' expected after new type");
        return false;
    }
    if (!CompileExpression(Segment))
        return false;
    if (Token.GetType() != SQUARE_CLOSE_BRACKET)
    {
        SetError("']' expected after new type[size");
        return false;
    }
    const uint32_t element_type = type;
    CompileToken(Segment, NEW_ARRAY, 1, &element_type, sizeof(uint32_t));
    CompileAuxiliaryTokens(Segment);
    return true;
}

// on a variable (current token): is "name[i].chain" all there is up to the closing ')' of a
// range-for, without a call or $? The position and the current token are kept.
bool COMPILER::IsPlainAttributePath()
{
    char *const at = Token.GetProgramControl();
    const std::string name = Token.GetData();
    bool plain = true;
    S_TOKEN_TYPE previous = UNKNOWN;
    for (uint32_t depth = 0; plain;)
    {
        const S_TOKEN_TYPE t = Token.Get();
        if (t == END_OF_PROGRAMM || t == SEPARATOR || t == BLOCK_IN || t == OP_DOLLAR)
            plain = false;
        else if (t == OPEN_BRACKET)
        {
            if (previous != DOT)
                plain = false; // a.f(...): a call
            depth++;
        }
        else if (t == SQUARE_OPEN_BRACKET)
            depth++;
        else if (t == CLOSE_BRACKET || t == SQUARE_CLOSE_BRACKET)
        {
            if (depth == 0)
                break;
            depth--;
        }
        if (t != DEBUG_LINEFEED && t != COMMENT)
            previous = t;
    }
    Token.SetProgramControl(at - name.size());
    Token.Get();
    return plain;
}
