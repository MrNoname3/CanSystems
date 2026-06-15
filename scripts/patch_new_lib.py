import os
import shutil

Import("env")

def patch_new_lib():
  # Get the framework directory path
  framework_dir = env.PioPlatform().get_package_dir("framework-arduino-avr")

  if framework_dir:
    # Define the path to the framework's original new.cpp and new.h
    new_cpp_path = os.path.join(framework_dir, "cores", "arduino", "new.cpp")
    new_h_path = os.path.join(framework_dir, "cores", "arduino", "new.h")

    # Define the path to your custom new.cpp and new.h files
    custom_new_cpp = os.path.join(env['PROJECT_DIR'], "patch", "new", "src", "new.cpp")
    custom_new_h = os.path.join(env['PROJECT_DIR'], "patch", "new", "src", "new.h")

    # Check if the framework's original files exist before replacing them
    if os.path.exists(new_cpp_path) and os.path.exists(new_h_path):
      # Check if the custom files exist
      if os.path.exists(custom_new_cpp) and os.path.exists(custom_new_h):
        print(f"Replacing {new_cpp_path} with custom new.cpp")
        shutil.copy(custom_new_cpp, new_cpp_path)
        print(f"Replacing {new_h_path} with custom new.h")
        shutil.copy(custom_new_h, new_h_path)
      else:
        print("Custom new.cpp and new.h files not found.")
    else:
      print(f"Original {new_cpp_path} or {new_h_path} not found, skipping replacement.")

patch_new_lib()
