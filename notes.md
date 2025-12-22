# Notes

rm -r -f ./droid_output
./build/tools/droid_tool/droid_tool --uber ../uber --output ./droid_output 

./build/tools/model_tool/model_tool --model-a ./droid_output/models/trap_head_small.gltf  

* shaders folder is not relative to provided asset path
* shaders folder does not exist in output of droid tool
* vertical offsets from old droidclasses seem crap. Manual adjustment is possible, add scale tool to determine automatic factor.
* add env maps
* unit tool not using same render path (no shader)