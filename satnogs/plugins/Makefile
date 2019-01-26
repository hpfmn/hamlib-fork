all: fm_plugin.so

%.so: %.cpp
	g++ -fPIC -shared -o lib$@ $<
