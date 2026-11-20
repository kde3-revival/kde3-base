
if include_kcontrol_access
ACCESS_SUBDIR=access
endif

if include_kcontrol_kfontinst
FONTINST_SUBDIR=kfontinst
endif

if include_kcontrol_randr
RANDR_SUBDIR=randr
endif

# 'colors' and 'fonts' should come after 'krdb'
SUBDIRS = bell background dnssd filetypes krdb input info ioslaveinfo kdm\
	kicker locale launch keys kio konq style kded \
	konqhtml ebrowsing kcontrol knotify\
	clock pics $(ACCESS_SUBDIR) colors energy fonts css \
	icons screensaver taskbar crypto privacy smserver \
	konsole spellchecking \
	nics $(FONTINST_SUBDIR) $(RANDR_SUBDIR) \
	componentchooser performance xinerama display kthememanager
