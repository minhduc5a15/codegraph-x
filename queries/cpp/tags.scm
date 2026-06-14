(namespace_definition
  name: (_) @name) @definition.namespace

(class_specifier
  name: (_) @name) @definition.class

(struct_specifier
  name: (_) @name) @definition.class

(union_specifier
  name: (_) @name) @definition.class

(enum_specifier
  name: (_) @name) @definition.enum

(enumerator
  name: (_) @name) @definition.enum_member

(type_definition
  declarator: (type_identifier) @name) @definition.type

(alias_declaration
  name: (type_identifier) @name) @definition.type

(function_definition
  declarator: (_) @name) @definition.function

(declaration
  declarator: (function_declarator declarator: (_) @name)) @definition.function

(field_declaration
  declarator: (_) @name) @definition.field

(declaration
  declarator: (identifier) @name) @definition.variable

(declaration
  declarator: (init_declarator declarator: (_) @name)) @definition.variable

(preproc_def
  name: (identifier) @name) @definition.macro

(preproc_function_def
  name: (identifier) @name) @definition.macro

(call_expression
  function: (_) @name) @reference.call

(base_class_clause
  [
    (type_identifier) @name
    (qualified_identifier) @name
    (template_type name: (_) @name)
  ] @reference.class)
