/*
 * MSSB (More Secure Secure Boot -- "Mosby") Secure Boot variables handling
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

#include "mosby.h"
#include "console.h"
#include "variables.h"

#define MOSBY_SETUP_MODE_ATTEMPTS   L"SetupModeAttempts"
#define MOSBY_LAST_INSTALL_TIME     L"LastInstallTime"

/* GUID we use to store variables */
STATIC EFI_GUID gMosbyVariableGuid =
	{ 0x26A35749, 0x477E, 0x41A4, { 0xA9, 0x0A, 0x19, 0xC3, 0xAF, 0xEA, 0x85, 0x40 } };

/* Various messages we display to the user as they attempt to enter Setup Mode */
STATIC CONST CHAR16 *AttemptMessage1[] = {
L"ERROR: Setup Mode is not enabled",
	L"",
	L"This platform is not in Secure Boot 'Setup Mode', which is      ",
	L"required for the installation of new Secure Boot keys.          ",
	L"",
	L"Enabling 'Setup Mode' usually requires going into your UEFI     ",
	L"firmware settings and manually changing the Secure Boot options.",
	L"",
	L"",
	L"Note: If you don't want to reinstall all certificates and       ",
	L"databases, but just ensure that the Secure Boot vulnerabilities,",
	L"that were known at the time this application was released, are  ",
	L"being properly revoked, you can select 'No' here and run Mosby  ",
	L"again with the '-u' option, which doesn't require 'Setup Mode'. ",
	L"",
	L"",
	L"Do you want to reboot to enable 'Setup Mode'?",
	L"",
	NULL
};
STATIC CONST CHAR16 *AttemptMessage2[] = {
	L"ERROR: Setup Mode is not enabled",
	L"",
	L"This platform is not in Secure Boot 'Setup Mode', which is      ",
	L"required for the installation of new Secure Boot keys.          ",
	L"",
	L"Enabling 'Setup Mode' usually requires going into your UEFI     ",
	L"firmware settings and manually changing the Secure Boot options.",
	L"",
	L"Do you want to reboot to enable 'Setup Mode'?",
	NULL
};
STATIC CONST CHAR16 *AttemptMessage3[] = {
	L"ERROR: Setup Mode is still not enabled",
	L"",
	L"It appears that you are still having trouble trying to enable   ",
	L"'Setup Mode'... If that is the case, you may try the following: ",
	L"",
	L"1)  In your UEFI settings, please locate your Secure Boot menu. ",
	L"    Again this may require disabling the 'CSM' option first.    ",
	L"2)  If you see an option between 'Standard' and 'Custom' mode   ",
	L"    for Secure Boot, make sure 'Custom' is selected.            ",
	L"3a) If you see an option (possibly under 'Key Management') to   ",
	L"    delete all variables or keys, please select it.             ",
	L"3b) Or if you see an option to enter 'Setup Mode', select that. ",
	L"4)  Make sure to save your changes by pressing <F10> and reboot.",
	L"",
	L"Do you want to reboot and try to enable 'Setup Mode' once more?",
	NULL
};
STATIC CONST CHAR16 *AttemptMessage4[] = {
	L"ERROR: Setup Mode is not enabled",
	L"",
	L"We are sorry to see that you are having so much trouble trying  ",
	L"to enable 'Setup Mode'. At this stage we would recommend that   ",
	L"you check your manufacturer's documentation or look for answers,",
	L"on user forums relevant to your hardware.                       ",
	L"",
	L"It is quite unfortunate that there exist manufacturers that make",
	L"it exceedingly difficult for users to set up their own Secure   ",
	L"Boot keys, despite the fact that 'Setup Mode' is part of the    ",
	L"UEFI specifications for the Secure Boot feature.                ",
	L"",
	L"We would therefore encourage you to reach out to your hardware  ",
	L"manufacturer, and let them know about the difficulties you are  ",
	L"encountering with trying to enable 'Setup Mode'...              ", 
	L"",
	L"Do you still want to reboot and try again?",
	NULL
};
STATIC CONST CHAR16 *ExitMessage1[] = {
	L"",
	L"Mosby has sucessfully installed the Secure Boot variables.       ",
	L"",
	L"You should now reboot into your UEFI firmware settings and make  ",
	L"sure that Secure Boot is enabled.                                ", 
	L"",
	L"IMPORTANT: Please make sure that you keep a copy of the following",
	L"generated files: 'MosbyKey.pfx', 'MosbyKey.crt', 'MosbyKey.key'. ",
	L"They are your UNIQUE SECURE BOOT SIGNING KEYS, and you should    ",
	L"carefully preserve them, so that you can sign UEFI executables.  ",
	L"",
	L"Also note that, if these files are present in the same directory ",
	L"as the application, Mosby will reuse them, thus allowing you to  ",
	L"reuse the same signing key across different systems.             "
	L"",
	L"Do you want to reboot into the UEFI Firmware Settings now?       ",
	NULL
};
STATIC CONST CHAR16 *ExitMessage2[] = {
	L"",
	L"Mosby has sucessfully installed the Secure Boot variables.      ",
	L"",
	L"You should reboot into your UEFI firmware settings and make sure",
	L"that Secure Boot is enabled.                                    ",
	L"",
	L"Do you want to reboot into the UEFI Firmware Settings now?      ",
	NULL
};

STATIC BOOLEAN
IsOsIndicationsSupported(
	IN CONST UINT64 Indication
)
{
	EFI_STATUS Status;
	UINT64 OsIndicationsSupported;
	UINTN Size = sizeof(OsIndicationsSupported);

	Status = gRT->GetVariable(EFI_OS_INDICATIONS_SUPPORT_VARIABLE_NAME,
		&gEfiGlobalVariableGuid, NULL, &Size, &OsIndicationsSupported);
	if (EFI_ERROR(Status))
		return FALSE;
	return OsIndicationsSupported & Indication;
}

STATIC EFI_STATUS SetOsIndication(
	IN CONST UINT64 Indication
)
{
	UINT64 OsIndications = 0;
	UINT32 Attributes = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS |
		EFI_VARIABLE_RUNTIME_ACCESS;
	UINTN Size = sizeof(OsIndications);

	gRT->GetVariable(EFI_OS_INDICATIONS_VARIABLE_NAME, &gEfiGlobalVariableGuid,
		&Attributes, &Size, &OsIndications);
	OsIndications |= Indication;
	return gRT->SetVariable(EFI_OS_INDICATIONS_VARIABLE_NAME, &gEfiGlobalVariableGuid,
		Attributes, Size, &OsIndications);
}

EFI_STATUS CheckSetupMode(VOID)
{
	EFI_STATUS Status;
	UINT8 SecureBoot = 0, SetupMode = 0, CustomMode = 0, SetupModeAttempts = 0;
	UINTN Size;
	INTN Sel;

	// Check the amount of times the user got into UEFI Setup to try to enable Setup
	// Mode, so that we can provide more helpful messages.
	Size = sizeof(SetupModeAttempts);
	gRT->GetVariable(MOSBY_SETUP_MODE_ATTEMPTS, &gMosbyVariableGuid, NULL, &Size, &SetupModeAttempts);
	// Now that we have (possibly) read it, delete the variable
	gRT->SetVariable(MOSBY_SETUP_MODE_ATTEMPTS, &gMosbyVariableGuid, 0, 0, NULL);

	// Get the Secure Boot and Setup Mode status
	Size = sizeof(SecureBoot);
	Status = gRT->GetVariable(EFI_SECURE_BOOT_MODE_NAME, &gEfiGlobalVariableGuid, NULL, &Size, &SecureBoot);
	if (EFI_ERROR(Status))
		ReportErrorAndExit(L"This platform does not support Secure Boot.\n");

	Size = sizeof(SetupMode);
	Status = gRT->GetVariable(EFI_SETUP_MODE_NAME, &gEfiGlobalVariableGuid, NULL, &Size, &SetupMode);

	if (EFI_ERROR(Status) || SecureBoot == SECURE_BOOT_MODE_ENABLE || SetupMode != SETUP_MODE) {
		RecallPrint(L"ERROR: Setup Mode not enabled (Attempt #%d)\n", SetupModeAttempts + 1);
		// Unfortunately, while EDK2 makes DeletePlatformKey() publicly available, which
		// should allow us to toggle Setup Mode, most modern platforms prevent the call
		// from working outside of the UEFI firmware settings.
		// Therefore we have little choice but to ask the user to reboot into UEFI Setup
		// (which we can help with, through EFI_OS_INDICATIONS_BOOT_TO_FW_UI), and wade
		// through options that are likely to look super confusing to the non initiated.
		// To alleviate some of that, we use the nonvolatile SetupModeAttempts variable
		// that detects how many times the user has been trying to get their platform
		// into Setup Mode, and alter our dialogs as a result.
		if (SetupModeAttempts == 0)
			Sel = ConsoleYesNo(AttemptMessage1);
		else if (SetupModeAttempts <= 2)
			Sel = ConsoleYesNo(AttemptMessage2);
		else if (SetupModeAttempts <= 5)
			Sel = ConsoleYesNo(AttemptMessage3);
		else
			Sel = ConsoleYesNo(AttemptMessage4);
		RecallPrintRestore();
		if (Sel == 0) {
			// Try to set Secure Boot to Custom Mode in case it helps. However, this is
			// NOT expected to do anything, since we're in runtime mode at this stage...
			CustomMode = CUSTOM_SECURE_BOOT_MODE;
			Size = sizeof(CustomMode);
			gRT->SetVariable(EFI_CUSTOM_MODE_NAME, &gEfiCustomModeEnableGuid,
					EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
					Size, &CustomMode);
			// Try PK deletion, in case it actually happens to work...
			DisablePKProtection();
			if (DeletePlatformKey() == EFI_SUCCESS) {
				RecallPrint(L"NOTICE: PK deletion successful! Platform should now be in Setup Mode.\n");
			} else if (IsOsIndicationsSupported(EFI_OS_INDICATIONS_BOOT_TO_FW_UI) && 
				SetOsIndication(EFI_OS_INDICATIONS_BOOT_TO_FW_UI) == EFI_SUCCESS) {
				// Log one more attempt by the user to try to enable Setup mode
				SetupModeAttempts++;
				gRT->SetVariable(MOSBY_SETUP_MODE_ATTEMPTS, &gMosbyVariableGuid,
					EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
					sizeof(SetupModeAttempts), &SetupModeAttempts);
			}
			RecallPrint(L"Rebooting platform...\n");
			gBS->Stall(2000000);
			gRT->ResetSystem(EfiResetWarm, EFI_SUCCESS, 0, NULL);
		} else {
			RecallPrint(L"Reboot operation cancelled by the user\n");
			Status = EFI_ABORTED;
		}
	}
	
exit:
	return Status;
}

VOID ExitNotice(
	IN CONST BOOLEAN KeysGenerated
)
{
	EFI_TIME Time = { 0 };
	INTN Sel;

	gRT->GetTime(&Time, NULL);
	// Store the last installation time, so we can alert the user if they re-run Mosby
	gRT->SetVariable(MOSBY_LAST_INSTALL_TIME, &gMosbyVariableGuid,
					EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
					sizeof(Time), &Time);
	Sel = ConsoleYesNo(KeysGenerated ? ExitMessage1 : ExitMessage2);
	RecallPrintRestore();
	if (Sel == 0) {
		if (IsOsIndicationsSupported(EFI_OS_INDICATIONS_BOOT_TO_FW_UI))
			SetOsIndication(EFI_OS_INDICATIONS_BOOT_TO_FW_UI);
		RecallPrint(L"Rebooting platform...\n");
		gBS->Stall(2000000);
		gRT->ResetSystem(EfiResetWarm, EFI_SUCCESS, 0, NULL);
	} else {
		RecallPrint(L"Reboot operation cancelled by the user\n");
	}
}
