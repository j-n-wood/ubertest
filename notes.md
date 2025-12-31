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


# Convert all models in directory (both .asc and .mdl)
model_tool --convert ./models/ -o ./output/

# Convert only ASC files  
model_tool --convert "./models/*.asc" -o ./output/

# Convert only MDL files
model_tool --convert "./models/*.mdl" -o ./output/

# Convert all supported files with wildcard
model_tool --convert "./models/*" -o ./output/


To run tests:

cd cpp-version
cmake -B build
cmake --build build --target run_tests
ctest --test-dir build --output-on-failure
Or run the executable directly:

./build/tests/run_tests

## level viewer
cmake --build build --target level_viewer 

 ./build/tools/level_viewer/level_viewer --input ./output/ships/ship1/levels 