#!/bin/bash

as_me=`basename -- "$0"`
as_test_x='test -x'

CC=gcc
ac_ext=c
ac_objext=o
ac_cpp='$CPP $CPPFLAGS'
ac_compile='$CC -c $CFLAGS $CPPFLAGS conftest.$ac_ext'
ac_link='$CC -o conftest$ac_exeext  $CFLAGS $CPPFLAGS $LDFLAGS conftest.$ac_ext $LIBS'
cache_file1=/dev/null
cache_file2=/dev/null

# as_fn_set_status STATUS
# -----------------------
# Set $? to STATUS, without forking.
as_fn_set_status ()
{
  return $1
} # as_fn_set_status

# ac_fn_c_try_link LINENO
# -----------------------
# Try to link conftest.$ac_ext, and return whether this succeeded.
ac_fn_c_try_link ()
{
#  echo "calling function: ac_fn_c_try_link ()"

  as_lineno=${as_lineno-"$1"} as_lineno_stack=as_lineno_stack=$as_lineno_stack
  rm -f conftest.$ac_objext conftest$ac_exeext

  if { { ac_try="$ac_link"
	case "(($ac_try" in
	  *\"* | *\`* | *\\*) ac_try_echo=\$ac_try;;
	  *) ac_try_echo=$ac_try;;
	esac
	eval ac_try_echo="\"\$as_me:${as_lineno-$LINENO}: $ac_try_echo\""
	echo "$ac_try_echo"; }  > $cache_file1
	(eval "$ac_link") 2> conftest.err
	ac_status=$?
  if test -s conftest.err; then
      grep -v '^ *+' conftest.err >conftest.er1
      cat conftest.er1 >& $cache_file1
      mv -f conftest.er1 conftest.err
  fi

  echo "$as_me:${as_lineno-$LINENO}: \$? = $ac_status" > $cache_file1
  test $ac_status = 0; } && {
	 test -z "$ac_c_werror_flag" ||
	 test ! -s conftest.err
       } && test -s conftest$ac_exeext && {
	 test "$cross_compiling" = yes ||
	 $as_test_x conftest$ac_exeext
       }; then :
  ac_retval=0
else
  echo "$as_me: failed program was:" > $cache_file1
sed 's/^/| /' conftest.$ac_ext > $cache_file1

	ac_retval=1
fi
  # Delete the IPA/IPO (Inter Procedural Analysis/Optimization) information
  # created by the PGI compiler (conftest_ipa8_conftest.oo), as it would
  # interfere with the next link command; also delete a directory that is
  # left behind by Apple's compiler.  We do this before executing the actions.
  rm -rf conftest.dSYM conftest_ipa8_conftest.oo
  eval $as_lineno_stack; test "x$as_lineno_stack" = x && { as_lineno=; unset as_lineno;}
  as_fn_set_status $ac_retval

} # ac_fn_c_try_link

# ================= main ==========================

#echo "$as_me: checking for library containing ax25_aton"


if test "${ac_cv_search_ax25_aton+set}" = set; then :
  echo "(cached) " > $cache_file2
else
  ac_func_search_save_LIBS=$LIBS

# in case file doesn't exist
touch confdefs.h

cat confdefs.h - <<_ACEOF >conftest.$ac_ext
/* end confdefs.h.  */

/* Override any GCC internal prototype to avoid an error.
   Use char because int might match the return type of a GCC
   builtin and then its argument prototype would still apply.  */
#ifdef __cplusplus
extern "C"
#endif
char ax25_aton ();
int
main ()
{
return ax25_aton ();
  ;
  return 0;
}
_ACEOF
for ac_lib in '' ax25; do
  if test -z "$ac_lib"; then
    ac_res="none required"
  else
    ac_res=-l$ac_lib
    LIBS="-l$ac_lib  $ac_func_search_save_LIBS"
  fi
#  echo "Calling ac_fn_c_try_link with ac_lib=$ac_lib LIBS=$LIBS"
  if ac_fn_c_try_link "$LINENO"; then :
    ac_cv_search_ax25_aton=$ac_res
  fi
rm -f core conftest.err conftest.$ac_objext \
   conftest$ac_exeext
  if test "${ac_cv_search_ax25_aton+set}" = set; then :
  break
fi
done
if test "${ac_cv_search_ax25_aton+set}" = set; then :

else
  ac_cv_search_ax25_aton=no
fi
rm conftest.$ac_ext
LIBS=$ac_func_search_save_LIBS
fi

{ echo "$as_me:${as_lineno-$LINENO}: result: $ac_cv_search_ax25_aton" > $cache_file1
echo "$ac_cv_search_ax25_aton" > $cache_file2; }
ac_res=$ac_cv_search_ax25_aton

# set exit code as if ax25 lib was not found
exitcode=1
if test "$ac_res" != no; then :
  test "$ac_res" = "none required" || LIBS="$ac_res $LIBS"
  exitcode=0
  HAVE_AX25_FLAG=HAVE_AX25_TRUE;
fi

rm confdefs.h

#echo "$as_me: end LIBS=$LIBS ac_cv_search_ax25_aton=$ac_cv_search_ax25_aton exit code = $exitcode"
echo $LIBS
exit $exitcode
