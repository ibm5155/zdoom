#include "dobject.h"
#include "sc_man.h"
#include "memarena.h"
#include "zcc_parser.h"
#include "zcc-parse.h"

class FLispString;
extern void (* const TreeNodePrinter[NUM_AST_NODE_TYPES])(FLispString &, ZCC_TreeNode *);

static const char *BuiltInTypeNames[] =
{
	"sint8", "uint8",
	"sint16", "uint16",
	"sint32", "uint32",
	"intauto",

	"bool",
	"float32", "float64", "floatauto",
	"string",
	"vector2",
	"vector3",
	"vector4",
	"name",
	"usertype"
};

class FLispString
{
public:
	operator FString &() { return Str; }

	FLispString()
	{
		NestDepth = Column = 0;
		WrapWidth = 72;
		NeedSpace = false;
		ConsecOpens = 0;
	}

	void Open(const char *label)
	{
		size_t labellen = label != NULL ? strlen(label) : 0;
		CheckWrap(labellen + 1 + NeedSpace);
		if (NeedSpace)
		{
			Str << ' ';
			ConsecOpens = 0;
		}
		Str << '(';
		ConsecOpens++;
		if (label != NULL)
		{
			Str.AppendCStrPart(label, labellen);
		}
		Column += labellen + 1 + NeedSpace;
		NestDepth++;
		NeedSpace = (label != NULL);
	}
	void Close()
	{
		assert(NestDepth != 0);
		Str << ')';
		Column++;
		NestDepth--;
		NeedSpace = true;
	}
	void Break()
	{
		// Don't break if not needed.
		if (Column != NestDepth)
		{
			if (NeedSpace)
			{
				ConsecOpens = 0;
			}
			else
			{ // Move hanging ( characters to the new line
				Str.Truncate(long(Str.Len() - ConsecOpens));
				NestDepth -= ConsecOpens;
			}
			Str << '\n';
			Column = NestDepth;
			NeedSpace = false;
			if (NestDepth > 0)
			{
				Str.AppendFormat("%*s", (int)NestDepth, "");
			}
			if (ConsecOpens > 0)
			{
				for (size_t i = 0; i < ConsecOpens; ++i)
				{
					Str << '(';
				}
				NestDepth += ConsecOpens;
			}
		}
	}
	bool CheckWrap(size_t len)
	{
		if (len + Column > WrapWidth)
		{
			Break();
			return true;
		}
		return false;
	}
	void Add(const char *str, size_t len)
	{
		CheckWrap(len + NeedSpace);
		if (NeedSpace)
		{
			Str << ' ';
		}
		Str.AppendCStrPart(str, len);
		Column += len + NeedSpace;
		NeedSpace = true;
	}
	void Add(const char *str)
	{
		Add(str, strlen(str));
	}
	void Add(FString &str)
	{
		Add(str.GetChars(), str.Len());
	}
	void AddName(FName name)
	{
		size_t namelen = strlen(name.GetChars());
		CheckWrap(namelen + 2 + NeedSpace);
		if (NeedSpace)
		{
			NeedSpace = false;
			Str << ' ';
		}
		Str << '\'' << name.GetChars() << '\'';
		Column += namelen + 2 + NeedSpace;
		NeedSpace = true;
	}
	void AddChar(char c)
	{
		Add(&c, 1);
	}
	void AddInt(int i, bool un=false)
	{
		char buf[16];
		size_t len;
		if (!un)
		{
			len = mysnprintf(buf, countof(buf), "%d", i);
		}
		else
		{
			len = mysnprintf(buf, countof(buf), "%uu", i);
		}
		Add(buf, len);
	}
	void AddHex(unsigned x)
	{
		char buf[10];
		size_t len = mysnprintf(buf, countof(buf), "%08x", x);
		Add(buf, len);
	}
	void AddFloat(double f, bool single)
	{
		char buf[32];
		size_t len = mysnprintf(buf, countof(buf), "%.4f", f);
		if (single)
		{
			buf[len++] = 'f';
			buf[len] = '\0';
		}
		Add(buf, len);
	}
private:
	FString Str;
	size_t NestDepth;
	size_t Column;
	size_t WrapWidth;
	size_t ConsecOpens;
	bool NeedSpace;
};

static void PrintNode(FLispString &out, ZCC_TreeNode *node)
{
	assert(TreeNodePrinter[NUM_AST_NODE_TYPES-1] != NULL);
	if (node->NodeType >= 0 && node->NodeType < NUM_AST_NODE_TYPES)
	{
		TreeNodePrinter[node->NodeType](out, node);
	}
	else
	{
		out.Open("unknown-node-type");
		out.AddInt(node->NodeType);
		out.Close();
	}
}

static void PrintNodes(FLispString &out, ZCC_TreeNode *node, bool newlist=true, bool addbreaks=false)
{
	ZCC_TreeNode *p;

	if (node == NULL)
	{
		out.Add("nil", 3);
	}
	else
	{
		if (newlist)
		{
			out.Open(NULL);
		}
		p = node;
		do
		{
			if (addbreaks)
			{
				out.Break();
			}
			PrintNode(out, p);
			p = p->SiblingNext;
		} while (p != node);
		if (newlist)
		{
			out.Close();
		}
	}
}

static void PrintBuiltInType(FLispString &out, EZCCBuiltinType type)
{
	assert(ZCC_NUM_BUILT_IN_TYPES == countof(BuiltInTypeNames));
	if (unsigned(type) >= unsigned(ZCC_NUM_BUILT_IN_TYPES))
	{
		char buf[30];
		size_t len = mysnprintf(buf, countof(buf), "bad-type-%u", type);
		out.Add(buf, len);
	}
	else
	{
		out.Add(BuiltInTypeNames[type]);
	}
}

static void PrintIdentifier(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_Identifier *inode = (ZCC_Identifier *)node;
	out.Open("identifier");
	out.AddName(inode->Id);
	out.Close();
}

static void PrintStringConst(FLispString &out, FString str)
{
	FString outstr;
	outstr << '"';
	for (size_t i = 0; i < str.Len(); ++i)
	{
		if (str[i] == '"')
		{
			outstr << "\"";
		}
		else if (str[i] == '\\')
		{
			outstr << "\\\\";
		}
		else if (str[i] >= 32)
		{
			outstr << str[i];
		}
		else
		{
			outstr.AppendFormat("\\x%02X", str[i]);
		}
	}
	outstr << '"';
	out.Add(outstr);
}

static void PrintClass(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_Class *cnode = (ZCC_Class *)node;
	out.Break();
	out.Open("class");
	out.AddName(cnode->ClassName);
	PrintNodes(out, cnode->ParentName);
	PrintNodes(out, cnode->Replaces);
	out.AddHex(cnode->Flags);
	PrintNodes(out, cnode->Body, false, true);
	out.Close();
}

static void PrintStruct(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_Struct *snode = (ZCC_Struct *)node;
	out.Break();
	out.Open("struct");
	out.AddName(snode->StructName);
	PrintNodes(out, snode->Body, false, true);
	out.Close();
}

static void PrintEnum(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_Enum *enode = (ZCC_Enum *)node;
	out.Break();
	out.Open("enum");
	out.AddName(enode->EnumName);
	PrintBuiltInType(out, enode->EnumType);
	out.Add(enode->Elements == NULL ? "nil" : "...", 3);
	out.Close();
}

static void PrintEnumTerminator(FLispString &out, ZCC_TreeNode *node)
{
	out.Open("enum-term");
	out.Close();
}

static void PrintStates(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_States *snode = (ZCC_States *)node;
	out.Break();
	out.Open("states");
	PrintNodes(out, snode->Body, false, true);
	out.Close();
}

static void PrintStatePart(FLispString &out, ZCC_TreeNode *node)
{
	out.Open("state-part");
	out.Close();
}

static void PrintStateLabel(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_StateLabel *snode = (ZCC_StateLabel *)node;
	out.Open("state-label");
	out.AddName(snode->Label);
	out.Close();
}

static void PrintStateStop(FLispString &out, ZCC_TreeNode *node)
{
	out.Open("state-stop");
	out.Close();
}

static void PrintStateWait(FLispString &out, ZCC_TreeNode *node)
{
	out.Open("state-wait");
	out.Close();
}

static void PrintStateFail(FLispString &out, ZCC_TreeNode *node)
{
	out.Open("state-fail");
	out.Close();
}

static void PrintStateLoop(FLispString &out, ZCC_TreeNode *node)
{
	out.Open("state-loop");
	out.Close();
}

static void PrintStateGoto(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_StateGoto *snode = (ZCC_StateGoto *)node;
	out.Open("state-goto");
	PrintNodes(out, snode->Label);
	PrintNodes(out, snode->Offset);
	out.Close();
}

static void PrintStateLine(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_StateLine *snode = (ZCC_StateLine *)node;
	out.Open("state-line");
	out.Add(snode->Sprite, 4);
	if (snode->bBright)
	{
		out.Add("bright", 6);
	}
	out.Add(*(snode->Frames));
	PrintNodes(out, snode->Offset);
	PrintNodes(out, snode->Action, false);
	out.Close();
}

static void PrintVarName(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_VarName *vnode = (ZCC_VarName *)node;
	out.Open("var-name");
	PrintNodes(out, vnode->ArraySize);
	out.AddName(vnode->Name);
	out.Close();
}

static void PrintType(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_Type *tnode = (ZCC_Type *)node;
	out.Open("bad-type");
	PrintNodes(out, tnode->ArraySize);
	out.Close();
}

static void PrintBasicType(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_BasicType *tnode = (ZCC_BasicType *)node;
	out.Open("basic-type");
	PrintNodes(out, tnode->ArraySize);
	PrintBuiltInType(out, tnode->Type);
	if (tnode->Type == ZCC_UserType)
	{
		PrintNodes(out, tnode->UserType, false);
	}
	out.Close();
}

static void PrintMapType(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_MapType *tnode = (ZCC_MapType *)node;
	out.Open("map-type");
	PrintNodes(out, tnode->ArraySize);
	PrintNodes(out, tnode->KeyType);
	PrintNodes(out, tnode->ValueType);
	out.Close();
}

static void PrintDynArrayType(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_DynArrayType *tnode = (ZCC_DynArrayType *)node;
	out.Open("dyn-array-type");
	PrintNodes(out, tnode->ArraySize);
	PrintNodes(out, tnode->ElementType);
	out.Close();
}

static void PrintClassType(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ClassType *tnode = (ZCC_ClassType *)node;
	out.Open("class-type");
	PrintNodes(out, tnode->ArraySize);
	PrintNodes(out, tnode->Restriction);
	out.Close();
}

static void OpenExprType(FLispString &out, EZCCExprType type)
{
	char buf[32];

	if (unsigned(type) < PEX_COUNT_OF)
	{
		mysnprintf(buf, countof(buf), "expr-%s", ZCC_OpInfo[type].OpName);
	}
	else
	{
		mysnprintf(buf, countof(buf), "bad-pex-%u", type);
	}
	out.Open(buf);
}

static void PrintExpression(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_Expression *enode = (ZCC_Expression *)node;
	OpenExprType(out, enode->Operation);
	out.Close();
}

static void PrintExprID(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ExprID *enode = (ZCC_ExprID *)node;
	assert(enode->Operation == PEX_ID);
	out.Open("expr-id");
	out.AddName(enode->Identifier);
	out.Close();
}

static void PrintExprTypeRef(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ExprTypeRef *enode = (ZCC_ExprTypeRef *)node;
	assert(enode->Operation == PEX_TypeRef);
	out.Open("expr-type-ref");
		 if (enode->RefType == TypeSInt8) { out.Add("sint8"); }
	else if (enode->RefType == TypeUInt8) { out.Add("uint8"); }
	else if (enode->RefType == TypeSInt16) { out.Add("sint16"); }
	else if (enode->RefType == TypeSInt32) { out.Add("sint32"); }
	else if (enode->RefType == TypeFloat32) { out.Add("float32"); }
	else if (enode->RefType == TypeFloat64) { out.Add("float64"); }
	else if (enode->RefType == TypeString) { out.Add("string"); }
	else if (enode->RefType == TypeName) { out.Add("name"); }
	else if (enode->RefType == TypeColor) { out.Add("color"); }
	else if (enode->RefType == TypeSound) { out.Add("sound"); }
	else { out.Add("other"); }
	out.Close();
}

static void PrintExprConstant(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ExprConstant *enode = (ZCC_ExprConstant *)node;
	assert(enode->Operation == PEX_ConstValue);
	out.Open("expr-const");
	if (enode->Type == TypeString)
	{
		PrintStringConst(out, *enode->StringVal);
	}
	else if (enode->Type == TypeFloat64)
	{
		out.AddFloat(enode->DoubleVal, false);
	}
	else if (enode->Type == TypeFloat32)
	{
		out.AddFloat(enode->DoubleVal, true);
	}
	else if (enode->Type == TypeName)
	{
		out.AddName(ENamedName(enode->IntVal));
	}
	else if (enode->Type->IsKindOf(RUNTIME_CLASS(PInt)))
	{
		out.AddInt(enode->IntVal, static_cast<PInt *>(enode->Type)->Unsigned);
	}
	out.Close();
}

static void PrintExprFuncCall(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ExprFuncCall *enode = (ZCC_ExprFuncCall *)node;
	assert(enode->Operation == PEX_FuncCall);
	out.Open("expr-func-call");
	PrintNodes(out, enode->Function);
	PrintNodes(out, enode->Parameters, false);
	out.Close();
}

static void PrintExprMemberAccess(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ExprMemberAccess *enode = (ZCC_ExprMemberAccess *)node;
	assert(enode->Operation == PEX_MemberAccess);
	out.Open("expr-member-access");
	PrintNodes(out, enode->Left);
	out.AddName(enode->Right);
	out.Close();
}

static void PrintExprUnary(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ExprUnary *enode = (ZCC_ExprUnary *)node;
	OpenExprType(out, enode->Operation);
	PrintNodes(out, enode->Operand, false);
	out.Close();
}

static void PrintExprBinary(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ExprBinary *enode = (ZCC_ExprBinary *)node;
	OpenExprType(out, enode->Operation);
	PrintNodes(out, enode->Left);
	PrintNodes(out, enode->Right);
	out.Close();
}

static void PrintExprTrinary(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ExprTrinary *enode = (ZCC_ExprTrinary *)node;
	OpenExprType(out, enode->Operation);
	PrintNodes(out, enode->Test);
	PrintNodes(out, enode->Left);
	PrintNodes(out, enode->Right);
	out.Close();
}

static void PrintFuncParam(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_FuncParm *pnode = (ZCC_FuncParm *)node;
	out.Break();
	out.Open("func-parm");
	out.AddName(pnode->Label);
	PrintNodes(out, pnode->Value, false);
	out.Close();
}

static void PrintStatement(FLispString &out, ZCC_TreeNode *node)
{
	out.Open("statement");
	out.Close();
}

static void PrintCompoundStmt(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_CompoundStmt *snode = (ZCC_CompoundStmt *)node;
	out.Break();
	out.Open("compound-stmt");
	PrintNodes(out, snode->Content, false, true);
	out.Close();
}

static void PrintContinueStmt(FLispString &out, ZCC_TreeNode *node)
{
	out.Break();
	out.Open("continue-stmt");
	out.Close();
}

static void PrintBreakStmt(FLispString &out, ZCC_TreeNode *node)
{
	out.Break();
	out.Open("break-stmt");
	out.Close();
}

static void PrintReturnStmt(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ReturnStmt *snode = (ZCC_ReturnStmt *)node;
	out.Break();
	out.Open("return-stmt");
	PrintNodes(out, snode->Values, false);
	out.Close();
}

static void PrintExpressionStmt(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ExpressionStmt *snode = (ZCC_ExpressionStmt *)node;
	out.Break();
	out.Open("expression-stmt");
	PrintNodes(out, snode->Expression, false);
	out.Close();
}

static void PrintIterationStmt(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_IterationStmt *snode = (ZCC_IterationStmt *)node;
	out.Break();
	out.Open("iteration-stmt");
	out.Add((snode->CheckAt == ZCC_IterationStmt::Start) ? "start" : "end");
	out.Break();
	PrintNodes(out, snode->LoopCondition);
	out.Break();
	PrintNodes(out, snode->LoopBumper);
	out.Break();
	PrintNodes(out, snode->LoopStatement);
	out.Close();
}

static void PrintIfStmt(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_IfStmt *snode = (ZCC_IfStmt *)node;
	out.Break();
	out.Open("if-stmt");
	PrintNodes(out, snode->Condition);
	out.Break();
	PrintNodes(out, snode->TruePath);
	out.Break();
	PrintNodes(out, snode->FalsePath);
	out.Close();
}

static void PrintSwitchStmt(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_SwitchStmt *snode = (ZCC_SwitchStmt *)node;
	out.Break();
	out.Open("switch-stmt");
	PrintNodes(out, snode->Condition);
	out.Break();
	PrintNodes(out, snode->Content, false);
	out.Close();
}

static void PrintCaseStmt(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_CaseStmt *snode = (ZCC_CaseStmt *)node;
	out.Break();
	out.Open("case-stmt");
	PrintNodes(out, snode->Condition, false);
	out.Close();
}

static void BadAssignOp(FLispString &out, int op)
{
	char buf[32];
	size_t len = mysnprintf(buf, countof(buf), "assign-op-%d", op);
	out.Add(buf, len);
}

static void PrintAssignStmt(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_AssignStmt *snode = (ZCC_AssignStmt *)node;
	out.Open("assign-stmt");
	switch (snode->AssignOp)
	{
	case ZCC_EQ:		out.AddChar('='); break;
	case ZCC_MULEQ:		out.Add("*=", 2); break;
	case ZCC_DIVEQ:		out.Add("/=", 2); break;
	case ZCC_MODEQ:		out.Add("%=", 2); break;
	case ZCC_ADDEQ:		out.Add("+=", 2); break;
	case ZCC_SUBEQ:		out.Add("-=", 2); break;
	case ZCC_LSHEQ:		out.Add("<<=", 2); break;
	case ZCC_RSHEQ:		out.Add(">>=", 2); break;
	case ZCC_ANDEQ:		out.Add("&=", 2); break;
	case ZCC_OREQ:		out.Add("|=", 2); break;
	case ZCC_XOREQ:		out.Add("^=", 2); break;
	default:			BadAssignOp(out, snode->AssignOp); break;
	}
	PrintNodes(out, snode->Dests);
	PrintNodes(out, snode->Sources);
	out.Close();
}

static void PrintLocalVarStmt(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_LocalVarStmt *snode = (ZCC_LocalVarStmt *)node;
	out.Open("local-var-stmt");
	PrintNodes(out, snode->Type);
	PrintNodes(out, snode->Vars);
	PrintNodes(out, snode->Inits);
	out.Close();
}

static void PrintFuncParamDecl(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_FuncParamDecl *dnode = (ZCC_FuncParamDecl *)node;
	out.Break();
	out.Open("func-param-decl");
	PrintNodes(out, dnode->Type);
	out.AddName(dnode->Name);
	out.AddHex(dnode->Flags);
	out.Close();
}

static void PrintConstantDef(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ConstantDef *dnode = (ZCC_ConstantDef *)node;
	out.Break();
	out.Open("constant-def");
	out.AddName(dnode->Name);
	PrintNodes(out, dnode->Value, false);
	out.Close();
}

static void PrintDeclarator(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_Declarator *dnode = (ZCC_Declarator *)node;
	out.Break();
	out.Open("declarator");
	out.AddHex(dnode->Flags);
	PrintNodes(out, dnode->Type);
	out.Close();
}

static void PrintVarDeclarator(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_VarDeclarator *dnode = (ZCC_VarDeclarator *)node;
	out.Break();
	out.Open("var-declarator");
	out.AddHex(dnode->Flags);
	PrintNodes(out, dnode->Type);
	PrintNodes(out, dnode->Names);
	out.Close();
}

static void PrintFuncDeclarator(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_FuncDeclarator *dnode = (ZCC_FuncDeclarator *)node;
	out.Break();
	out.Open("func-declarator");
	out.AddHex(dnode->Flags);
	PrintNodes(out, dnode->Type);
	out.AddName(dnode->Name);
	PrintNodes(out, dnode->Params);
	PrintNodes(out, dnode->Body, false);
	out.Close();
}

void (* const TreeNodePrinter[NUM_AST_NODE_TYPES])(FLispString &, ZCC_TreeNode *) =
{
	PrintIdentifier,
	PrintClass,
	PrintStruct,
	PrintEnum,
	PrintEnumTerminator,
	PrintStates,
	PrintStatePart,
	PrintStateLabel,
	PrintStateStop,
	PrintStateWait,
	PrintStateFail,
	PrintStateLoop,
	PrintStateGoto,
	PrintStateLine,
	PrintVarName,
	PrintType,
	PrintBasicType,
	PrintMapType,
	PrintDynArrayType,
	PrintClassType,
	PrintExpression,
	PrintExprID,
	PrintExprTypeRef,
	PrintExprConstant,
	PrintExprFuncCall,
	PrintExprMemberAccess,
	PrintExprUnary,
	PrintExprBinary,
	PrintExprTrinary,
	PrintFuncParam,
	PrintStatement,
	PrintCompoundStmt,
	PrintContinueStmt,
	PrintBreakStmt,
	PrintReturnStmt,
	PrintExpressionStmt,
	PrintIterationStmt,
	PrintIfStmt,
	PrintSwitchStmt,
	PrintCaseStmt,
	PrintAssignStmt,
	PrintLocalVarStmt,
	PrintFuncParamDecl,
	PrintConstantDef,
	PrintDeclarator,
	PrintVarDeclarator,
	PrintFuncDeclarator
};

FString ZCC_PrintAST(ZCC_TreeNode *root)
{
	FLispString out;
	PrintNodes(out, root);
	return out;
}
