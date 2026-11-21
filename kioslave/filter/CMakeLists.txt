kde3_add_module(kio_filter
  SOURCES
    filter.cc
  LINK
    KDE3::kio
)

install(FILES
  gzip.protocol
  # kdelibs' kxzfilter, kzstdfilter and klz4filter
  xz.protocol
  lzma.protocol
  zstd.protocol
  lz4.protocol
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})
# include_BZIP2 (kdelibs' kbzip2filter)
if(BZIP2_FOUND)
  install(FILES
    bzip.protocol
    bzip2.protocol
    DESTINATION ${KDE3_SERVICES_INSTALL_DIR})
endif()
