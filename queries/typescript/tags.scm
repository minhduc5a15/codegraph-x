(class_declaration
  name: (_) @name) @definition.class

(function_declaration
  name: (_) @name) @definition.function

(method_definition
  name: (_) @name) @definition.method

(call_expression
  function: (_) @name) @reference.call

(lexical_declaration
  (variable_declarator
    name: (_) @name
    value: (arrow_function))) @definition.function
