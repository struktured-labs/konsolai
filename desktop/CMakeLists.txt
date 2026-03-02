

########### install files ###############

install( PROGRAMS org.kde.konsolai.desktop  DESTINATION ${KDE_INSTALL_APPDIR} )
install( FILES org.kde.konsolai.appdata.xml DESTINATION ${KDE_INSTALL_METAINFODIR} )
install(FILES org.kde.konsolai.desktop DESTINATION ${KDE_INSTALL_DATADIR}/kglobalaccel)

install(FILES konsolai.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
install( PROGRAMS konsolairun.desktop  DESTINATION ${KDE_INSTALL_DATADIR}/kio/servicemenus )

# Install notification sound files
# KNotification resolves Sound= paths relative to /usr/share/sounds/
install(FILES
    ${CMAKE_SOURCE_DIR}/data/sounds/permission.wav
    ${CMAKE_SOURCE_DIR}/data/sounds/waiting.wav
    ${CMAKE_SOURCE_DIR}/data/sounds/complete.wav
    ${CMAKE_SOURCE_DIR}/data/sounds/error.wav
    ${CMAKE_SOURCE_DIR}/data/sounds/yolo.wav
    DESTINATION ${KDE_INSTALL_DATADIR}/sounds/konsolai
)
