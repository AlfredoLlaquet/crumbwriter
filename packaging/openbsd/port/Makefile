COMMENT =	plain-text editor for novels, stories and numbered crumbs
DISTNAME =	crumbwriter-3.2
CATEGORIES =	editors
HOMEPAGE =	https://github.com/AlfredoLlaquet/crumbwriter
SITES =		${HOMEPAGE}/releases/download/v3.2/
EXTRACT_SUFX =	.tar.xz

# Original code: Unlicense. Word data and artwork: see NOTICE.md.
PERMIT_PACKAGE = Yes
MODULES =	devel/cmake x11/qt6
MODQT6_DEPS =	No
LIB_DEPENDS =	archivers/libarchive devel/glib2 textproc/hunspell x11/qt6/qtbase
RUN_DEPENDS =	print/poppler,-utils devel/desktop-file-utils x11/gtk+4,-guic
WANTLIB +=	${COMPILER_LIBCXX} Qt6Core Qt6Gui Qt6Network Qt6Widgets Qt6Xml archive c
WANTLIB +=	gio-2.0 glib-2.0 gobject-2.0 hunspell-1.7 m
CONFIGURE_ARGS = -DBUILD_TESTING=OFF
NO_TEST =	Yes

post-install:
	${INSTALL_DATA_DIR} ${PREFIX}/share/crumbwriter/dictionaries ${PREFIX}/share/crumbwriter/thesaurus
	${INSTALL_DATA} ${WRKSRC}/word-data/dictionaries/* ${PREFIX}/share/crumbwriter/dictionaries/
	${INSTALL_DATA} ${WRKSRC}/word-data/thesaurus/* ${PREFIX}/share/crumbwriter/thesaurus/
.for component in MyThes SCOWL
	${INSTALL_DATA_DIR} ${PREFIX}/share/doc/crumbwriter/third-party/${component}
	${INSTALL_DATA} ${WRKSRC}/word-data/third-party/${component}/* ${PREFIX}/share/doc/crumbwriter/third-party/${component}/
.endfor

.include <bsd.port.mk>
