--TEST--
runkit_method_add() function
--SKIPIF--
<?php if(!extension_loaded("runkit") || !RUNKIT_FEATURE_MANIPULATION) print "skip"; ?>
--INI--
error_reporting=E_ALL
display_errors=on
--FILE--
<?php
class runkit_class {
}

runkit_method_add('runkit_class', 'runkit_method', '$a, $b = "bar"', 'echo "a is $a\nb is $b\n";'); 
runkit_class::runkit_method('foo','bar');
?>
--EXPECTF--
Strict Standards: Non-static method runkit_class::runkit_method() should not be called statically in %s on line %d
a is foo
b is bar
