#! /bin/bash
#############################################################################
# Copyright (C) 2007-2013 Laboratório de Sistemas e Tecnologia Subaquática  #
# Departamento de Engenharia Electrotécnica e de Computadores               #
# Rua Dr. Roberto Frias, 4200-465 Porto, Portugal                           #
#############################################################################
# Author: Ricardo Martins                                                   #
#############################################################################
# This script will add the svn:keywords property to all files in the        #
# repository.                                                               #
#############################################################################

find src programs -type f | grep -v svn | while read file; do
    svn propset svn:keywords Id "$file"
done
