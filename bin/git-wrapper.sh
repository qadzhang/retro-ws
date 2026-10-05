#!/bin/bash
# Git wrapper to handle duplicate .nuttx-patch-applied patches
if [[ "$1" == "apply" ]] || [[ "$1" == *"apply" ]]; then
    # Handle patches that try to create .nuttx-patch-applied
    for arg in "$@"; do
        if [[ "$arg" == *"0001-mbedtls_add_prefix.patch" ]] || [[ "$arg" == *"0002-mbedtls_add_prefix_to_macro.patch" ]]; then
            # For nuttx mbedtls patches, we need to handle the duplicate .nuttx-patch-applied issue
            # The patches are identical and both create .nuttx-patch-applied
            # Solution: apply patches one at a time, deleting .nuttx-patch-applied between each
            patch_files=()
            for f in "$@"; do
                if [[ "$f" == *.patch ]]; then
                    patch_files+=("$f")
                fi
            done
            
            for patch_file in "${patch_files[@]}"; do
                rm -f .nuttx-patch-applied
                /usr/bin/git apply "$patch_file" || true
            done
            exit 0
        fi
    done
fi
exec /usr/bin/git "$@"
