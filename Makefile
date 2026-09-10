CXX ?= g++
.DEFAULT_GOAL := all
PKG_CONFIG ?= pkg-config
TARGET ?= build/pegasusg_by_roc.exe

SDL_PACKAGES := sdl2 SDL2_image SDL2_ttf
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags $(SDL_PACKAGES)) -Isrc
CXXFLAGS ?= -O2 -g -std=c++17 -Wall -Wextra -Wpedantic
CXXFLAGS += -pthread
CPPFLAGS += -MMD -MP
LDLIBS += $(shell $(PKG_CONFIG) --libs $(SDL_PACKAGES))
LDLIBS += $(shell $(PKG_CONFIG) --libs alsa 2>/dev/null)
LDLIBS += -pthread

SOURCES := \
	src/main.cpp \
	src/gba_frontend.cpp \
	src/gba_preferences.cpp \
	src/gba_state.cpp \
	src/gba_ui_state.cpp \
	src/h700_services.cpp \
	src/library_scanner.cpp \
	src/optimized_image_path.cpp \
	src/pegasus_metadata.cpp \
	src/video_preview.cpp

OBJECTS := $(patsubst src/%.cpp,build/obj/%.o,$(SOURCES))
DEPENDS := $(OBJECTS:.o=.d)

.PHONY: all clean test screenshots

.PHONY: packzip
packzip:
	PEGASUSG_SKIP_BUILD=1 PEGASUSG_OUTPUT=Zip $(if $(VERSION),PEGASUSG_VERSION="$(VERSION)") bash H700/build_app.sh

build/game_search_test.exe: tests/game_search_test.cpp src/game_search.h src/pinyin_table.inc
	@mkdir -p $(dir $@)
	$(CXX) -Isrc $(CXXFLAGS) $< -o $@

build/gba_frontend_interaction_test.exe: tests/gba_frontend_interaction_test.cpp $(filter-out build/obj/main.o,$(OBJECTS))
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

test: test-interaction

.PHONY: test-interaction
test-interaction: build/game_search_test.exe build/gba_frontend_interaction_test.exe
	./build/game_search_test.exe
	./build/gba_frontend_interaction_test.exe

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(OBJECTS) -o $@ $(LDLIBS)

build/obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/layout_test.exe: tests/layout_test.cpp src/layout.cpp src/layout.h src/ui_types.h
	@mkdir -p $(dir $@)
	$(CXX) -Isrc $(CXXFLAGS) tests/layout_test.cpp src/layout.cpp -o $@

build/catalog_test.exe: tests/catalog_test.cpp src/library_catalog.cpp src/cover_grid_data_source.cpp src/library_catalog.h src/cover_grid_data_source.h src/ui_types.h
	@mkdir -p $(dir $@)
	$(CXX) -Isrc $(CXXFLAGS) tests/catalog_test.cpp src/library_catalog.cpp src/cover_grid_data_source.cpp -o $@

build/online_sources_test.exe: tests/online_sources_test.cpp src/online_sources.cpp src/online_sources.h src/ui_types.h
	@mkdir -p $(dir $@)
	$(CXX) -Isrc $(CXXFLAGS) tests/online_sources_test.cpp src/online_sources.cpp -o $@

build/pegasus_metadata_test.exe: tests/pegasus_metadata_test.cpp src/pegasus_metadata.cpp src/pegasus_metadata.h src/gba_model.h
	@mkdir -p $(dir $@)
	$(CXX) -Isrc $(CXXFLAGS) tests/pegasus_metadata_test.cpp src/pegasus_metadata.cpp -o $@

build/gba_ui_state_test.exe: tests/gba_ui_state_test.cpp src/gba_ui_state.cpp src/gba_ui_state.h
	@mkdir -p $(dir $@)
	$(CXX) -Isrc $(CXXFLAGS) tests/gba_ui_state_test.cpp src/gba_ui_state.cpp -o $@

build/gba_preferences_test.exe: tests/gba_preferences_test.cpp src/gba_preferences.cpp src/gba_preferences.h
	@mkdir -p $(dir $@)
	$(CXX) -Isrc $(CXXFLAGS) tests/gba_preferences_test.cpp src/gba_preferences.cpp -o $@

build/gba_state_test.exe: tests/gba_state_test.cpp src/gba_state.cpp src/gba_state.h src/gba_model.h
	@mkdir -p $(dir $@)
	$(CXX) -Isrc $(CXXFLAGS) tests/gba_state_test.cpp src/gba_state.cpp -o $@

build/optimized_image_path_test.exe: tests/optimized_image_path_test.cpp src/optimized_image_path.cpp src/optimized_image_path.h
	@mkdir -p $(dir $@)
	$(CXX) -Isrc $(CXXFLAGS) tests/optimized_image_path_test.cpp src/optimized_image_path.cpp -o $@

build/video_preview_stop_test.exe: tests/video_preview_stop_test.cpp tests/fake_ffmpeg_block.sh src/video_preview.cpp src/video_preview.h
	@mkdir -p $(dir $@) build/test-bin
	cp tests/fake_ffmpeg_block.sh build/test-bin/ffmpeg
	chmod +x build/test-bin/ffmpeg
	$(CXX) -Isrc $(CXXFLAGS) tests/video_preview_stop_test.cpp src/video_preview.cpp -o $@ $(shell $(PKG_CONFIG) --libs alsa 2>/dev/null)

test: build/layout_test.exe build/catalog_test.exe build/online_sources_test.exe build/pegasus_metadata_test.exe build/gba_ui_state_test.exe build/gba_preferences_test.exe build/gba_state_test.exe build/optimized_image_path_test.exe build/video_preview_stop_test.exe
	./build/layout_test.exe
	./build/catalog_test.exe
	./build/online_sources_test.exe
	./build/pegasus_metadata_test.exe
	./build/gba_ui_state_test.exe
	./build/gba_preferences_test.exe
	./build/gba_state_test.exe
	./build/optimized_image_path_test.exe
	PATH="$(CURDIR)/build/test-bin:$$PATH" ./build/video_preview_stop_test.exe
	sh tests/filter_mode_test.sh
	sh tests/auto_cheats_test.sh
	sh tests/autostart_test.sh
	sh tests/suspend_test.sh
	sh tests/game_overrides_test.sh
	sh tests/game_volume_test.sh
	sh tests/recommended_controls_test.sh
	sh tests/splash_mode_test.sh

screenshots: $(TARGET)
	@mkdir -p screenshots
	SDL_VIDEODRIVER=dummy ./$(TARGET) --width 720 --height 480 --screenshot screenshots/pegasusg-by-roc-720x480.png

clean:
	rm -rf build

-include $(DEPENDS)
