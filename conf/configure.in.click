dnl autoconf rules to find click - copied from dmalloc example

AC_ARG_WITH(click,	--with-click=path specify a pathname for the click modular router, d="$withval", d="")

CLICK_VERS=1.3

CLICK_PATH="$PWD/../click \
	$PWD/../../click \
	$PWD/../click-$CLICK_VERS \
	$PWD/../../click-$CLICK_VERS \
	$PWD/../click/include \
	$PWD/../../click/include \
	$PWD/../click-$CLICK_VERS/include \
	$PWD/../../click-$CLICK_VERS/include \
	$PWD/../click/ns \
	$PWD/../../click/ns \
	$PWD/../click-$CLICK_VERS/ns \
	$PWD/../../click-$CLICK_VERS/ns \
	"
CLICK_PATH_D="$d \
	$d/lib \
	$d/include \
	"

NS_BEGIN_PACKAGE(click)
NS_CHECK_HEADER_PATH(click/simclick.h,$CLICK_PATH,$d,$CLICK_PATH_D,V_HEADER_CLICK,click)
NS_CHECK_LIB_PATH(nsclick,$CLICK_PATH,$d,$CLICK_PATH_D,V_LIB_CLICK,click)
V_DEFINES="-DCLICK_NS $V_DEFINES"
NS_END_PACKAGE(click,yes)
