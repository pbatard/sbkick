#!/bin/env bash
# This script generates the C source for the data we embed in Mosby.

# The binaries we want to embedd and their URLs
declare -A source=(
  [kek_ms1.cer]='https://go.microsoft.com/fwlink/?LinkId=321185'
  [kek_ms2.cer]='https://go.microsoft.com/fwlink/?linkid=2239775'
  # Revoked by Microsoft. Can't have this if you have dbx_update_2024_all.bin
  # since the latter adds the former into the DBX database:
  # [db_ms1.cer]='https://go.microsoft.com/fwlink/?linkid=321192'
  [db_ms2.cer]='https://go.microsoft.com/fwlink/?linkid=321194'
  [db_ms3.cer]='https://go.microsoft.com/fwlink/?linkid=2239776'
  [db_ms4.cer]='https://go.microsoft.com/fwlink/?linkid=2239872'
  [dbx_x64.bin]='https://uefi.org/sites/default/files/resources/x64_DBXUpdate.bin'
  [dbx_ia32.bin]='https://uefi.org/sites/default/files/resources/x86_DBXUpdate.bin'
  [dbx_aa64.bin]='https://uefi.org/sites/default/files/resources/arm64_DBXUpdate.bin'
  [dbx_arm.bin]='https://uefi.org/sites/default/files/resources/arm_DBXUpdate.bin'
  # Shim does not provide an SBatLevel.txt we can download, so we currently use our own.
  # See: https://github.com/rhboot/shim/issues/685
  [sbat_level.txt]='https://github.com/pbatard/Mosby/raw/main/data/sbat_level.txt'
  # Microsoft SSP variables... provided by Red Hat, since Microsoft doesn't understand the
  # importance of letting everyone access CRITICAL PIECES OF A PUBLIC SECURITY TRUST CHAIN!
  [ssp_var_defs.h]='https://raw.githubusercontent.com/rhboot/shim/main/include/ssp_var_defs.h'
  # Found in %WINDIR%\System32\SecureBootUpdates\ on recent versions of Windows, these are
  # the new DBX updates, that Microsoft are about to apply to all systems... but that they
  # are again not making PUBLICLY AVAILABLE TO EVERYONE!
  [dbx_update_2024_all.bin]='https://github.com/pbatard/Mosby/raw/main/data/dbx_update_2024_all.bin'
  [dbx_update_svn_all.bin]='https://github.com/pbatard/Mosby/raw/main/data/dbx_update_svn_all.bin'
)

# Optional description for specific files
# Needs to be updated manually on DBX update since Microsoft stupidly decided to
# hardcode the EFI_TIME timestamp of ALL authenticated list updates to 2010.03.06
# instead of using the actual timestamp of when they create the variables...
declare -A description=(
  [dbx_x64.bin]='DBX for x86 (64 bit) [2023.05.09]'
  [dbx_ia32.bin]='DBX for x86 (32 bit) [2023.05.09]'
  [dbx_aa64.bin]='DBX for ARM (64 bit) [2023.05.09]'
  [dbx_arm.bin]='DBX for ARM (32 bit) [2023.05.09]'
  [dbx_update_2024_all.bin]="Revocation of 'Microsoft Windows Production PCA 2011'"
  [dbx_update_svn_all.bin]="Microsoft's 'Secure Version Number' DBX entries [2024.08]"
)

declare -A archguard=(
  [x64]='#if defined(_M_X64) || defined(__x86_64__)'
  [ia32]='#if defined(_M_IX86) || defined(__i386__)'
  [aa64]='#if defined (_M_ARM64) || defined(__aarch64__)'
  [arm]='#if defined (_M_ARM) || defined(__arm__)'
  [riscv64]='#if defined(_M_RISCV64) || (defined (__riscv) && (__riscv_xlen == 64))'
)

declare -A ssp_varname=(
  [SSPU]='SkuSiPolicyUpdateSigners'
  [SSPV]='SkuSiPolicyVersion'
)

cat << EOF
/* Autogenerated file - DO NOT EDIT */
/*
 * MSSB (More Secure Secure Boot -- "Mosby") embedded data
 * Copyright © 2024 Pete Batard <pete@akeo.ie>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include "mosby.h"
#include "ssp_var_defs.h"

EOF

# Get the SSP date from the last GitHub commit of ssp_var_defs.h
ssp_date_url="${source[ssp_var_defs.h]}"
ssp_date_url="${ssp_date_url#*main/}"
ssp_date_url="${ssp_date_url//\//%2F}"
ssp_date_url="https://api.github.com/repos/rhboot/shim/commits?path=${ssp_date_url}&page=1&per_page=1"
ssp_date="$(curl -s -L ${ssp_date_url} | grep -m1 -Eo '[0-9]+\-[0-9]+\-[0-9]+')"
ssp_date=${ssp_date//-/.}

for file in "${!source[@]}"; do
  # '-o' will try to use an override from the current repo
  if [[ "$1" == "-o" &&  -f ../data/${file} ]]; then
    cp ../data/${file} .
  else
    curl -f -s -L ${source[${file}]} -o ${file} || { echo "Failed to retreive ${source[${file}]}"; exit 1; }
  fi
  if [[ "${file}" = "ssp_var_defs.h" ]]; then
    continue
  fi
  echo "// From ${source[${file}]}"
  if [[ "${description[${file}]}" == "" ]]; then
    type=${file%%_*}
    if [[ "$type" == "sbat" ]]; then
      while IFS=, read -r c1 c2 c3; do
        if [ "$c1" = "sbat" ]; then
          date="[${c3:0:4}.${c3:4:2}.${c3:6:2}]"
          break
        fi
      done < ${file}
      description[${file}]="SbatLevel.txt $date"
    elif [[ "$type" == "db" || "$type" == "kek" ]]; then
      description[${file}]="$(openssl x509 -noout -subject -in ${file} | sed -n '/^subject/s/^.*CN = //p')"
    else
      echo "ERROR: No description for ${file}"
      exit 1
    fi
  fi
  xxd -i ${file}
  echo ""
  rm ${file}
done

# Break down ssp_var_defs.h into 2 distinct SSPU and SSPV entries
source[sspu_var_defs.h]=${source[ssp_var_defs.h]}
source[sspv_var_defs.h]=${source[ssp_var_defs.h]}
unset source[ssp_var_defs.h]

echo "EFI_STATUS InitializeList("
echo "	IN OUT MOSBY_LIST *List"
echo ")"
echo "{"
echo "	if (MOSBY_MAX_LIST_SIZE < ${#source[@]})"
echo "		return EFI_INVALID_PARAMETER;"
echo "	ZeroMem(List, sizeof(MOSBY_LIST));"
for file in "${!source[@]}"; do
  data=${file%\.*}_${file##*\.}
  type=${file%%_*}
  type=${type^^}
  arch=${file%\.*}
  arch=${arch##*_}
  if [[ "$type" == "DBX" && "$arch" != "all" ]]; then
    echo "${archguard[$arch]}"
  elif [ "$type" == "SSP" ]; then
    type="SSPU"
  fi
  echo "	List->Entry[List->Size].Type = ${type};"
  if [[ "$type" == "SBAT" || "$type" == "SSPU" || "$type" == "SSPV" ]]; then
    echo "	List->Entry[List->Size].Flags = USE_BUFFER;"
  fi
  if [[ "$type" == "SBAT" || "$type" == "MOK" || "$type" == "SSPU" || "$type" == "SSPV" ]]; then
    echo "	List->Entry[List->Size].Attrs = UEFI_VAR_NV_BS;"
  else
    echo "	List->Entry[List->Size].Attrs = UEFI_VAR_NV_BS_RT_TIMEAUTH;"
  fi
  echo "	List->Entry[List->Size].Path = L\"${file}\";"
  echo "	List->Entry[List->Size].Url = \"${source[${file}]}\";"
  if [[ "$type" == "SSPU" || "$type" == "SSPV" ]]; then
    echo "	List->Entry[List->Size].Description = \"${ssp_varname[${type}]} [${ssp_date}]\";"
    echo "	List->Entry[List->Size].Buffer.Data = ${ssp_varname[${type}]};"
    echo "	List->Entry[List->Size].Buffer.Size = sizeof(${ssp_varname[${type}]});"
  else
    echo "	List->Entry[List->Size].Description = \"${description[${file}]}\";"
    echo "	List->Entry[List->Size].Buffer.Data = ${data};"
    echo "	List->Entry[List->Size].Buffer.Size = ${data}_len;"
  fi
  echo "	List->Size++;"
  if [[ "$type" == "DBX" && "$arch" != "all" ]]; then
    echo "#endif"
  fi
done
echo "	return EFI_SUCCESS;"
echo "}"
