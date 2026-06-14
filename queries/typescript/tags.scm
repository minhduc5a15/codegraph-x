; === Namespaces / Modules ===
(internal_module
  name: (identifier) @name) @definition.namespace

; === Classes ===
(class_declaration
  name: (type_identifier) @name) @definition.class

(function_declaration
  name: (identifier) @name) @definition.function

(lexical_declaration
  (variable_declarator
    name: (identifier) @name
    value: (arrow_function))) @definition.function

(method_definition
  name: (property_identifier) @name) @definition.method

(class_heritage
  (extends_clause 
    value: (identifier) @name) @reference.base)

(class_heritage
  (implements_clause 
    (type_identifier) @name) @reference.base)

; === Calls ===
(call_expression
  function: (identifier) @name) @reference.call

(call_expression
  function: (member_expression
    property: (property_identifier) @name)) @reference.call