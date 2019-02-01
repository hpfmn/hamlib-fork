all: fm_plugin.so wfm_plugin.so

%.so: %.cpp
	g++ -fPIC -shared -o lib$@ $<
