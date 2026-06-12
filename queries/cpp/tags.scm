(namespace_definition
  name: (_) @name) @definition.namespace

(class_specifier
  name: (_) @name) @definition.class

(struct_specifier
  name: (_) @name) @definition.class

(function_definition
  declarator: (_) @name) @definition.function

(call_expression
  function: (_) @name) @reference.call

(base_class_clause
  [
    (type_identifier) @name
    (qualified_identifier) @name
    (template_type name: (_) @name)
  ] @reference.class)
