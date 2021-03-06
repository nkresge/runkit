--TEST--
runkit_method_redefine() function
--SKIPIF--
<?php if(!extension_loaded("runkit") || !RUNKIT_FEATURE_MANIPULATION) print "skip"; ?>
--INI--
error_reporting=E_ALL
display_errors=on
--FILE--
<?php
class runkit_class {
	static function runkit_method($a) {
		echo "a is $a\n";
	}
	static function runkitMethod($a) {
		echo "a is $a\n";
	}
}
runkit_class::runkit_method('foo');
runkit_method_redefine('runkit_class','runkit_method','$b', 'echo "b is $b\n";', RUNKIT_ACC_STATIC);
runkit_class::runkit_method('bar');
runkit_class::runkitMethod('foo');
runkit_method_redefine('runkit_class','runkitMethod','$b', 'echo "b is $b\n";', RUNKIT_ACC_STATIC);
runkit_class::runkitMethod('bar');
?>
--EXPECT--
a is foo
b is bar
a is foo
b is bar
