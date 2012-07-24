--TEST--
runkit_default_property_add() function
--SKIPIF--
<?php
// runkit_default_property_add doesn't work in 5.4 and is undocumented and unused.
?>
<?php
    // The method runkit_default_property_add is undocumented or unused, so skip this test.
    print "skip";
?>
<?php if(!extension_loaded("runkit") || !RUNKIT_FEATURE_MANIPULATION) print "skip";
      if(array_shift(explode('.', PHP_VERSION)) < 5) print "skip";
?>
--INI--
error_reporting=E_ALL
display_errors=on
--FILE--
<?php
class RunkitClass {
}

$className = 'RunkitClass';
$propName = 'publicProperty';
runkit_default_property_add($className, 'constArray', array('a'=>1));
runkit_default_property_add($className, $propName, 1, RUNKIT_ACC_PUBLIC);
runkit_default_property_add($className, 'privateProperty', "a", RUNKIT_ACC_PRIVATE);
runkit_default_property_add($className, 'protectedProperty', NULL, RUNKIT_ACC_PROTECTED);
$obj = new $className();
runkit_default_property_add($className, 'dynamic', $obj);
print_r(new $className());

runkit_default_property_add('stdClass', 'str', 'test');

$obj = new stdClass();
print_r($obj);
?>
--EXPECTF--
RunkitClass Object
(
    [constArray] => Array
        (
            [a] => 1
        )

    [publicProperty] => 1
    [privateProperty%sprivate] => a
    [protectedProperty:protected] =>%w
    [dynamic] => RunkitClass Object
        (
            [constArray] => Array
                (
                    [a] => 1
                )

            [publicProperty] => 1
            [privateProperty%sprivate] => a
            [protectedProperty:protected] =>%w
        )

)

Warning: runkit_default_property_add(): Adding properties to internal classes is not allowed in %s on line %d
stdClass Object
(
)
