SUBDIRS = plastik b2 default keramik laptop modernsystem quartz redmond web
# need to be ported: kwmtheme (almost done)

messages: rc.cpp
	$(EXTRACTRC) `find . -name \*.ui` >> rc.cpp
	$(XGETTEXT) `find . -name \*.cpp` -o $(podir)/kwin_clients.pot
	-rm rc.cpp
