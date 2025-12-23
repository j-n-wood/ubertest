# Notes

rm -r -f ./droid_output
./build/tools/droid_tool/droid_tool --uber ../uber --output ./droid_output 

./build/tools/model_tool/model_tool --model-a ./droid_output/models/trap_head_small.gltf

./build/tools/unit_test/unit_test --asset-path  ./droid_output droid_class_3.json


From cpp_version:

cmake --build build --target model_tool

./build/tools/model_tool/model_tool --convert ../uber/uberdroid/models/legs.mdl -o ./legs.gltf

* shaders folder is not relative to provided asset path
* shaders folder does not exist in output of droid tool
* vertical offsets from old droidclasses seem crap. Manual adjustment is possible.
* add env maps
