#!/usr/bin/env python
# -*- coding: utf-8 -*-
from __future__ import unicode_literals, print_function
import re
from Queue import Queue
from pypeg2 import *
import ast

"""
REQUIREMENTS:
pip install pypeg2
"""



GRAMMAR = """
<definition> := define [type] <name> = <expression>;
<assumption> := assume <name>: <expression>;

<expression> :=   lambda <name>: <expression>. <expression>
				| pi <name>: <expression>. <expression>
				| lambda rec <funcname> (<param>:<type-expression>): <returntype-expression>. <expression>
				| let <definition>... in <expression> end
				| <extract-expression> [<extract-expression>]+ [-> <expression>]
<extract-expression> := <atomic-expression> [ '[' <int> ']' ]+
<atomic-expression> := Nat | * | Param | Var
"""



TEST = """
define Nat = Nat;
assume n1: Nat;
assume opPlus: Nat -> Nat -> Nat;

define test1 = let
		define x = n1;
		define y = n1;
	in opPlus (x, y) end;

define test2 = let in Nat end;

define test3 = lambda n:Nat. let
	define r1 = lambda rec (x:Nat): Nat. r2 x;
	define r2 = lambda rec (y:Nat): Nat. r1 n1;
in (r1 n, r2 n) end;

"""



class Scope:
	def __init__(self):
		self.vars = {}
		self.depth = 0

	def push_param(self, vname=None, vtype=None):
		scope = self.push()
		if vname:
			scope.vars[vname] = ('param', vtype, self.depth)
		return scope

	def push(self):
		scope = Scope()
		scope.vars = self.vars.copy()
		scope.depth = self.depth + 1
		return scope

	def add_definition(self, defname, deftype=None):
		self.vars[defname] = ('def', deftype, 0)

	def contains(self, name):
		return name in self.vars


class CppCodeGen:
	def __init__(self):
		self.decls = []
		self.temp_varname_counter = 0
		self.unknown_identifiers = []
		self.pending_decls = []

	# Manage list of CPP lines

	def add_comment(self, comment):
		self.decls.append(('comment', comment))

	def add_decl(self, name, decl):
		self.decls.append(('decl', decl))
		# add pending decls if preconditions are fulfilled
		for (inst, depends) in self.pending_decls:
			depends.discard(name)

	def add_inst(self, inst):
		self.decls.append(('inst', inst))

	def add_blank(self):
		self.decls.append(('blank', ''))

	def add_depending(self, inst, depends_on):
		if len(depends_on) == 0:
			return self.add_inst(inst)
		self.pending_decls.append((inst, set(depends_on)))

	# add all "resolved" dependencies
	def progress_depending(self):
		for (inst, depends) in self.pending_decls:
			if len(depends) == 0:
				self.add_inst(inst)
		self.pending_decls = [(i, d) for i, d in self.pending_decls if len(d) > 0]

	# Unique variable names
	def get_temp_varname(self):
		self.temp_varname_counter += 1
		return 'lbl_tmp_'+str(self.temp_varname_counter)

	# Handling unknown identifiers
	def unknown_identifier(self, ident):
		self.unknown_identifiers.append(ident)

	def get_unknown_identifiers(self):
		return self.unknown_identifiers

	def set_unknown_identifiers_list(self, l):
		self.unknown_identifiers = l

	# Returning generated code and status

	def output(self, indent=''):
		return '\n'.join([indent + y for x, y in self.decls])

	def warnings(self):
		warnings = []
		for x in set(self.unknown_identifiers):
			warnings.append('[WARN] Unknown variable: '+str(x))
		for i, d in self.pending_decls:
			warnings.append('[WARN] Recursive declaration waiting for {} to appear'.format(', '.join(d)))
		return warnings


class AstCreator:
	def __init__(self):
		self.queue = Queue()
		self.seen = [] # set does not work, as nodes are unhashable / mutable

	def append(self, node, scope, callback):
		if node not in self.seen:
			self.seen.append(node)
			self.queue.put((node, scope, callback))

	def progress_all(self):
		while not self.queue.empty():
			node, scope, callback = self.queue.get()
			obj = node.to_ast(scope, self)
			callback(obj)




class LambdaAST:
	"""
	Removes all syntactic sugar from the AST.
	@:return the new representation of this node
	"""
	def normalize(self):
		if isinstance(self, List):
			for i in xrange(len(self)):
				if isinstance(self[i], LambdaAST):
					self[i] = self[i].normalize()
		for (k, v) in self.__dict__.items():
			if isinstance(v, LambdaAST):
				self.__dict__[k] = v.normalize()
		return self


def cpp_string(x):
	return '"'+x.replace('"', r'\"')+'"'





Expression = []
AtomicExpression = []
Keyword.regex = re.compile(r'\w+|\*')
Symbol.regex = re.compile(r'\w+|\*')
Symbol.check_keywords = True


class Identifier(str, LambdaAST):
	grammar = name()

	def to_cpp(self, scope, codegen, opts={}):
		# check if var in scope
		if self.name not in scope.vars:
			codegen.unknown_identifier(self.name)
			return str(self.name)
		# resolve var
		kind, vartype, acc = scope.vars[self.name]
		if kind == 'param':
			return 'w.var(' + vartype.to_cpp(scope, codegen, {'accept_inline_tuple': True}) + ', ' + str(scope.depth - acc) + ', ' + cpp_string(self.name) + ')'
		return str(self.name)

	def __repr__(self):
		return 'Identifier('+repr(self.name)+')'

	def to_ast(self, scope, astcreator):
		if self.name not in scope.vars:
			asm = ast.get_assumption(self.name)
			if asm:
				return asm
			raise Exception('Unknown value (TODO): '+repr(self.name))
		kind, vartype, acc = scope.vars[self.name]
		return vartype


class Constant(Symbol, LambdaAST):
	grammar = Enum(Symbol('*'), Symbol('Nat'))

	def compose(self, parser, attr_of):
		return self.name

	def to_cpp(self, scope, codegen, opts={}):
		if self.name == '*':
			return 'w.star()'
		elif self.name == 'Nat' and not scope.contains(self.name):
			return 'w.nat()'
		else:
			return self.name

	def to_ast(self, scope, astcreator):
		return ast.get_constant(self.name)


class Tupel(List, LambdaAST):
	grammar = '(', csl(Expression), ')'

	def compose(self, parser, attr_of):
		return '('+', '.join([parser.compose(x, attr_of=self) for x in self])+')'

	def to_cpp(self, scope, codegen, opts={}):
		if len(self) == 1:
			return self[0].to_cpp(scope, codegen, opts)
		else:
			tuple = '{'+', '.join([element.to_cpp(scope, codegen) for element in self])+'}'
			if not 'accept_inline_tuple' in opts:
				tuple = 'w.tuple('+tuple+')'
			return tuple

	def to_ast(self, scope, astcreator):
		if len(self) == 1:
			return self[0].to_ast(scope, astcreator)
		else:
			return ast.Tupel([element.to_ast(scope, astcreator) for element in self])


class Lambda(Plain, LambdaAST):
	grammar = ['lambda', '\\', 'λ'], blank, [name(), '_'], ':', attr('type', Expression), '.', blank, attr('body', Expression)

	def compose(self, parser, attr_of):
		return 'λ ' + self.name + ':' + parser.compose(self.type, attr_of=self) + '. ' + parser.compose(self.body, attr_of=self)

	def to_cpp(self, scope, codegen, opts={}):
		paramtype = self.type.to_cpp(scope, codegen, {'accept_inline_tuple':True})
		return 'w.lambda('+paramtype+', '+self.body.to_cpp(scope.push_param(self.name, self.type), codegen)+')'

	def to_ast(self, scope, astcreator):
		tp = self.type.to_ast(scope, astcreator)
		param = ast.ParamDef(self.name, tp)
		return ast.Lambda([param, self.body.to_ast(scope.push_param(self.name, param), astcreator)])


class LambdaRec(Plain, LambdaAST):
	#lambda rec [name](param:type): returntype. expression
	grammar = ['lambda', '\\', 'λ'], blank, K('rec'), blank, optional(name()), \
			  '(', attr('param', [name(), '_']), ':', attr('type', Expression), ')', blank, \
			  ':', blank, attr('returntype', Expression), '.', blank, attr('body', Expression)

	def __init__(self):
		self.name = None

	def compose(self, parser, attr_of):
		result = 'λ rec ';
		if self.name:
			result += self.name + ' ';
		result += '(' + self.param.thing + ':' + parser.compose(self.type, attr_of=self) + ') : '
		result += parser.compose(self.returntype, attr_of=self) + '. ' + parser.compose(self.body, attr_of=self)
		return result

	def to_cpp(self, scope, codegen, opts={}):
		# declare variable without body
		paramtype = self.type.to_cpp(scope, codegen, {'accept_inline_tuple':True});
		returntype = self.returntype.to_cpp(scope, codegen);
		decl = 'w.lambda_rec(' + paramtype + ', ' + returntype;
		if self.name:
			decl += ', '+cpp_string(self.name)
		decl += ')'
		if 'cpp_name' in opts:
			varname = opts['cpp_name']
			result = decl
		else:
			varname = self.name or codegen.get_temp_varname()
			codegen.add_decl(varname, 'auto {} = {};'.format(varname, decl))
			scope.add_definition(varname, Pi.new('_', self.type, self.returntype))
			result = varname

		# body decl
		childscope = scope.push_param(self.param.thing, self.type)
		old_unknown = codegen.get_unknown_identifiers()
		codegen.set_unknown_identifiers_list([])
		body = self.body.to_cpp(childscope, codegen)
		codegen.add_depending('{}->set({});'.format(varname, body), codegen.get_unknown_identifiers())
		codegen.set_unknown_identifiers_list(old_unknown)
		return result

	def to_ast(self, scope, astcreator):
		tp = self.type.to_ast(scope, astcreator)
		param = ast.ParamDef(self.param.thing, tp)
		pscope = scope.push_param(self.param.thing, param)
		result = ast.LambdaNominal([param, self.returntype.to_ast(pscope, astcreator)])
		scope.add_definition(self.name, result)
		pscope.add_definition(self.name, result)
		# enqueue body
		astcreator.append(self.body, pscope, lambda n: result.set_body(n))
		return result


class Pi(Plain, LambdaAST):
	grammar = ['pi', 'Π'], blank, [name(), '_'], ':', attr('type', Expression), '.', blank, attr('body', Expression)

	@staticmethod
	def new(pname, ptype, body):
		pi = Pi()
		pi.name = pname or '_'
		pi.type = ptype
		pi.body = body
		return pi

	def compose(self, parser, attr_of):
		# shorten "Π_:X. Y" to "X -> Y"
		if self.name == '_':
			return parser.compose(self.type, attr_of=self) + ' -> ' + parser.compose(self.body, attr_of=self)
		return 'Π ' + self.name + ':' + parser.compose(self.type, attr_of=self) + '. ' + parser.compose(self.body, attr_of=self)

	def to_cpp(self, scope, codegen, opts={}):
		paramtype = self.type.to_cpp(scope, codegen, {'accept_inline_tuple':True})
		return 'w.pi('+paramtype+', '+self.body.to_cpp(scope.push_param(self.name, self.type), codegen)+')'

	def to_ast(self, scope, astcreator):
		tp = self.type.to_ast(scope, astcreator)
		param = ast.ParamDef(self.name, tp)
		return ast.Pi([param, self.body.to_ast(scope.push_param(self.name, ast.ParamDef(self.name, tp)), astcreator)])


class Extract(List, LambdaAST):
	grammar = attr('base', AtomicExpression), maybe_some('[', re.compile(r'\d+'), ']')

	"""def normalize(self):
		# remove Extract without indices
		if len(self) == 0:
			return self.base.normalize()
		else:
			return LambdaAST.normalize(self)"""

	def to_cpp(self, scope, codegen, opts={}):
		if len(self) == 0:
			return self.base.to_cpp(scope, codegen, opts)
		result = self.base.to_cpp(scope, codegen)
		for index in self:
			result = 'w.extract('+result+', '+index+')'
		return result

	def to_ast(self, scope, astcreator):
		result = self.base.to_ast(scope, astcreator)
		for index in self:
			result = ast.Extract([result, index])
		return result


class SpecialFunction(Symbol, LambdaAST):
	grammar = Enum(Symbol('sigma'), Symbol('tuple'))


class SpecialApp(Plain, LambdaAST):
	grammar = attr('func', SpecialFunction), attr('param', Extract)

	def compose(self, parser, attr_of):
		return self.func+' '+parser.compose(self.param, attr_of=self)

	def to_cpp(self, scope, codegen, opts={}):
		if self.func.name == 'sigma':
			return 'w.sigma('+self.param.to_cpp(scope, codegen, {'accept_inline_tuple': True})+')'
		elif self.func.name == 'tuple':
			return 'w.tuple('+self.param.to_cpp(scope, codegen, {'accept_inline_tuple': True})+')'
		else:
			raise Exception("Unknown special function ("+str(self.func)+")")

	def to_ast(self, scope, astcreator):
		tpl = self.param.to_ast(scope, astcreator)
		if self.func.name == 'sigma':
			return ast.Sigma(tpl.ops)
		return tpl




class App(List, LambdaAST):
	grammar = attr('func', [SpecialApp, Extract]), maybe_some(Extract), optional('->', attr('arrow', Expression))

	def __init__(self):
		self.arrow = None

	def normalize(self):
		# convert -> to pi
		if self.arrow is not None:
			pi = Pi.new('_', self, self.arrow)
			self.arrow = None
			return pi.normalize()
		# remove App without parameters
		#elif len(self) == 0:
		#	return self.func.normalize()
		else:
			return LambdaAST.normalize(self)

	def to_expr(self):
		return self.func.to_expr() + ' '.join([x.to_expr() for x in self])

	def compose(self, parser, attr_of):
		result = parser.compose(self.func, attr_of=self)
		for param in self:
			result += ' ' + parser.compose(param, attr_of=self)
		return result

	def to_cpp(self, scope, codegen, opts={}):
		if len(self) == 0:
			return self.func.to_cpp(scope, codegen, opts)
		result = self.func.to_cpp(scope, codegen)
		for param in self:
			result = 'w.app('+result+', '+param.to_cpp(scope, codegen, {'accept_inline_tuple':True})+')'
		return result

	def __repr__(self):
		result = '('+repr(self.func);
		for param in self:
			result += '@'+repr(param)
		return result + ')'

	def to_ast(self, scope, astcreator):
		result = self.func.to_ast(scope, astcreator)
		for param in self:
			result = ast.App([result, param.to_ast(scope, astcreator)])
		return result


class InnerDefinition(Plain, LambdaAST):
	grammar = K('define'), blank, flag('type', 'type'), name(), blank, '=', blank, attr("body", Expression), ';'

	def compose(self, parser, attr_of):
		return self.name+' := '+parser.compose(self.body)

	def to_cpp(self, scope, codegen, opts={}):
		codegen.add_comment('// '+compose(self, autoblank=True))
		body = self.body.to_cpp(scope, codegen, {'cpp_name': self.name})
		scope.add_definition(self.name)
		codegen.add_decl(self.name, 'auto '+self.name+' = '+body+';')

	def to_ast(self, scope, astcreator):
		body = self.body.to_ast(scope, astcreator)
		scope.add_definition(self.name, body)
		return [(str(self.name), body)]


class Definition(InnerDefinition):
	def to_cpp(self, scope, codegen, opts={}):
		InnerDefinition.to_cpp(self, scope, codegen)
		codegen.add_inst('printValue('+self.name+');')
		if not self.type:
			codegen.add_inst('printType(' + self.name + ');')
		codegen.progress_depending()
		codegen.add_blank()


class Assumption(Plain, LambdaAST):
	grammar = K('assume'), blank, name(), ':', blank, attr("body", Expression), ';'

	def to_expr(self):
		return 'assume '+self.name+': '+self.body.to_expr()

	def to_cpp(self, scope, codegen, opts={}):
		codegen.add_comment('// '+compose(self, autoblank=True));
		body = self.body.to_cpp(scope, codegen)
		scope.add_definition(self.name, self.body)
		codegen.add_decl(self.name, 'auto '+self.name+' = w.assume('+body+', '+cpp_string(self.name)+');')
		codegen.add_inst('printType(' + self.name + ');')
		codegen.progress_depending()
		codegen.add_blank()

	def to_ast(self, scope, astcreator):
		if ast.has_predefined_assumption(self.name):
			assumption = ast.get_assumption(self.name)
			# TODO assert type equality
		else:
			assumption = ast.Assume([self.name, self.body.to_ast(scope, astcreator)])
		scope.add_definition(self.name, assumption)
		return [(str(self.name), assumption)]


class Comment(str, LambdaAST):
	grammar = [comment_cpp, comment_sh, comment_c]

	def to_cpp(self, scope, codegen, opts={}):
		return ''

	def to_ast(self, scope, astcreator):
		return []



class LetIn(List, LambdaAST):
	grammar = K('let'), blank, maybe_some([InnerDefinition, Comment]), blank, K('in'), blank, attr('expr', Expression), blank, K('end')

	def compose(self, parser, attr_of):
		return 'let {{ {} }} in {} end'.format(', '.join([parser.compose(d, attr_of=self) for d in self]), parser.compose(self.expr, attr_of=self))

	def to_cpp(self, scope, codegen, opts={}):
		inner_scope = scope.push()
		for definition in self:
			definition.to_cpp(inner_scope, codegen)
		return self.expr.to_cpp(inner_scope, codegen)

	def to_ast(self, scope, astcreator):
		inner_scope = scope.push()
		for definition in self:
			# this also adds the created nodes to inner_scope
			definition.to_ast(inner_scope, astcreator)
		return self.expr.to_ast(inner_scope, astcreator)



Expression.extend([LambdaRec, Lambda, Pi, LetIn, App])
AtomicExpression.extend([Tupel, Constant, Identifier])



class Program(List, LambdaAST):
	grammar = -1, [Definition, Assumption, Comment]

	def to_cpp(self):
		scope = Scope()
		codegen = CppCodeGen()
		for x in self:
			x.to_cpp(scope, codegen)
		codegen.progress_depending()
		codegen.add_blank()
		for warn in codegen.warnings():
			print(warn)
		return codegen.output()

	def to_ast(self):
		scope = Scope()
		astcreator = AstCreator()
		defs = []
		for x in self:
			defs += x.to_ast(scope, astcreator)
		astcreator.progress_all()
		return defs





def parse_lambda_code(s):
	return parse(s, Program).normalize()


if __name__ == '__main__':
	program = parse_lambda_code(TEST)
	print(program)
	print('\n')
	print(program.to_cpp())
	pass