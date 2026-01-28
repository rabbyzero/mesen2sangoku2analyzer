import os

file_path = 'Core/Core.vcxproj'

banned_phrases = [
    'Include="GBA\\', 
    'Include="Gameboy\\', 
    'Include="PCE\\',
    'Include="SMS\\', 
    'Include="SNES\\', 
    'Include="WS\\'
]

with open(file_path, 'r') as f:
    lines = f.readlines()

new_lines = []
for line in lines:
    if any(phrase in line for phrase in banned_phrases):
        continue
    new_lines.append(line)

with open(file_path, 'w') as f:
    f.writelines(new_lines)
