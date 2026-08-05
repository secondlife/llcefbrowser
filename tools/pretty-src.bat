@if not exist "tools\" (
    @echo Run this command from the project root directory
) else (
    astyle.exe --project=tools\astylerc include\*.h

    astyle.exe --project=tools\astylerc src\*.cpp
    astyle.exe --project=tools\astylerc src\*.h

    astyle.exe --project=tools\astylerc examples\single-browser\src\*.cpp
    astyle.exe --project=tools\astylerc examples\single-browser\src\*.h

    astyle.exe --project=tools\astylerc examples\multiple-browsers\src\*.cpp
    astyle.exe --project=tools\astylerc examples\multiple-browsers\src\*.h
 )
