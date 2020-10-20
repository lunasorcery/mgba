include(ExportDirectory)
set(SOURCE_FILES
	hle3d.c
	backends/asterix.c
	backends/drome.c)

source_group("HLE3D" FILES ${SOURCE_FILES})

export_directory(HLE3D SOURCE_FILES)
