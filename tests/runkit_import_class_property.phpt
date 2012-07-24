--TEST--
runkit_import() Importing and overriding class properties
--SKIPIF--
<?php
// Skipping this because runkit_import is unused in tag web code.
echo 'skip';
?>
<?php if(!extension_loaded("runkit") || !RUNKIT_FEATURE_MANIPULATION) print "skip";
      if(array_shift(explode(".", PHP_VERSION)) < 5) print "skip";
?>
--FILE--
<?php
class Test {
    public static $s = "s";
    public $v = "v";
}

$o = new Test;
echo $o->v, "\n";
echo Test::$s, "\n";
runkit_import(dirname(__FILE__) . '/runkit_import_class_property.inc', RUNKIT_IMPORT_CLASS_PROPS);
$o = new Test;
echo $o->v, "\n";
echo Test::$s, "\n";
runkit_import(dirname(__FILE__) . '/runkit_import_class_property.inc', RUNKIT_IMPORT_CLASS_PROPS | RUNKIT_IMPORT_CLASS_STATIC_PROPS | RUNKIT_IMPORT_OVERRIDE);
$o = new Test;
echo $o->v, "\n";
echo Test::$s, "\n";
?>
--EXPECTF--
v
s

Notice: runkit_import(): Test->v already exists, not importing in %s on line %d
v
s
IMPORTED: v
IMPORTED: s
