; === Classes ===
(class_definition
  name: (identifier) @name) @definition.class

; === Functions & Methods ===
; Method (Function nằm trong Class body)
(class_definition
  body: (block
    (function_definition
      name: (identifier) @name) @definition.method))

(function_definition
  name: (identifier) @name) @definition.function

; === Inheritance ===
(class_definition
  superclasses: (argument_list
    (identifier) @name) @reference.base)
    
; === Calls ===
(call
  function: (identifier) @name) @reference.call

(call
  function: (attribute
    attribute: (identifier) @name)) @reference.call