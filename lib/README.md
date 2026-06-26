# Metal Library - mtl_library

Collection of various mtl (Metal) libs. My attempt to streamline most of the \*.mtl
files in order to join forces :)

1. Extract/order/categorize/rename essential/important functions into `lib` folder.
2. to provide a testframework support this process

This will basically build up a standard library where file can be included on demand & functions re-used in a structured way. We'd avoid writing same functions over and over again.
Ideally each function has a test and documentation & follows same coding standards. I know this is very ambitious, but its at least a start... Thanks for your contributions!

## Projects

- https://code.google.com/archive/p/nabaztag-source-code/source/default/source
- https://github.com/OpenJabNab/OpenJabNab
- https://github.com/rngtng/NabaztagHackKit
- https://github.com/andreax79/ServerlessNabaztag
- https://github.com/ccarlo64/nab_mqtt


### Testing

The lib includes a simple test framework to test the functions. See `test/*`. A typical test looks like this:

```c
 let test "math operations" -> t in
  (
    //assertions
    assert_equalI 0 10 - (2 * 5);
  0);
```

The framework offers assertions similar to [Ruby Test::Unit](http://ruby-doc.org/stdlib-1.9.3/libdoc/test/unit/rdoc/Test/Unit.html) style. Mind that the variable type has to be given
explicit. Convention is:

  * I = integer
  * S = string
  * L = list
  * T = table

Following assertions are available (see bytecode/test/helper.mtl)

  * assert_equalI I I
  * assert_equalI S S
  * assert_nil I
  * assert_equalIL
  * assert_equalSL
  * assert_equalTL
