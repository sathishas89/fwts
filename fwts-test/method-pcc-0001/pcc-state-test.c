/*
 * Copyright (C) 2026 Canonical
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "fwts.h"
#include "fwts_acpi_object_eval.h"

static void check(const bool condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "%s\n", message);
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char **argv)
{
	fwts_framework_test test = { .name = "pcc-state" };
	fwts_framework fw = {
		.current_major_test = &test,
		.current_minor_test_num = 1,
		.filter_level = LOG_LEVEL_ALL,
	};
	ACPI_BUFFER buf;
	ACPI_STATUS status;
	ACPI_HANDLE parent;
	bool pcc_unsupported;

	check(argc == 2, "Expected an ACPI dump filename");
	fw.acpi_table_acpidump_file = argv[1];
	fw.results = fwts_log_open(test.name, "/dev/null", "w", LOG_TYPE_PLAINTEXT);
	check(fw.results != NULL, "Cannot open the test log");
	check(fwts_summary_init() == FWTS_OK, "Cannot initialize result summaries");
	check(fwts_acpi_init(&fw) == FWTS_OK, "Cannot initialize the ACPI fixture");

	/* Consuming the marker must leave no state for a subsequent caller. */
	fwts_acpi_pcc_handler_called_set(true);
	check(fwts_acpi_pcc_handler_called_get_and_clear(), "PCC marker was not captured");
	check(!fwts_acpi_pcc_handler_called_get_and_clear(), "PCC marker was not consumed");

	status = fwts_acpi_object_evaluate_with_pcc(&fw, "\\_SB_.PMC0._GAI",
		NULL, &buf, &pcc_unsupported);
	check(status == AE_SUPPORT && pcc_unsupported, "PCC evaluation was not identified");
	check(!fwts_acpi_pcc_handler_called_get_and_clear(), "PCC state survived evaluation");
	free(buf.Pointer);
	fwts_acpi_object_evaluate_report_error(&fw, "\\_SB_.PMC0._GAI",
		status, pcc_unsupported);
	check(fw.minor_tests.skipped == 1 && fw.minor_tests.failed == 0,
		"Unsupported PCC evaluation was not skipped");

	/* No new evaluation resets the marker between these two reports. */
	fwts_acpi_object_evaluate_report_error(&fw, "unrelated", AE_SUPPORT, false);
	check(fw.minor_tests.skipped == 1 && fw.minor_tests.failed == 1 &&
		(fw.failed_level & LOG_LEVEL_HIGH), "Unrelated AE_SUPPORT was not a failure");

	/* The existing evaluation interface must also consume PCC state. */
	status = fwts_acpi_object_evaluate(&fw, "\\_SB_.PMC1._GAI", NULL, &buf);
	check(status == AE_SUPPORT, "Expected an unsupported PCC evaluation");
	check(!fwts_acpi_pcc_handler_called_get_and_clear(), "Legacy evaluation retained PCC state");
	free(buf.Pointer);

	/* Exercise the separate handle-based evaluation path. */
	check(AcpiGetHandle(NULL, "\\_SB_.PMC0", &parent) == AE_OK,
		"Cannot find the PCC device");
	fwts_evaluate_found_method(&fw, &parent, "_GHL", NULL, NULL, NULL);
	check(fw.minor_tests.skipped == 2 && fw.minor_tests.failed == 1,
		"Handle-based PCC evaluation was not skipped");
	check(!fwts_acpi_pcc_handler_called_get_and_clear(), "Handle-based evaluation retained PCC state");
	fwts_acpi_object_evaluate_report_error(&fw, "unrelated", AE_SUPPORT, false);
	check(fw.minor_tests.skipped == 2 && fw.minor_tests.failed == 2,
		"Unrelated AE_SUPPORT after handle-based evaluation was not a failure");

	/* Successful and unrelated failing evaluations must return fresh metadata. */
	fwts_acpi_pcc_handler_called_set(true);
	status = fwts_acpi_object_evaluate_with_pcc(&fw, "\\_SB_.GOOD._GAI",
		NULL, &buf, &pcc_unsupported);
	check(status == AE_OK && !pcc_unsupported, "Successful evaluation inherited PCC state");
	check(buf.Pointer != NULL && ((ACPI_OBJECT *)buf.Pointer)->Type == ACPI_TYPE_INTEGER &&
		((ACPI_OBJECT *)buf.Pointer)->Integer.Value == 42, "Unexpected ordinary method result");
	check(!fwts_acpi_pcc_handler_called_get_and_clear(), "Successful evaluation retained PCC state");
	free(buf.Pointer);

	pcc_unsupported = true;
	status = fwts_acpi_object_evaluate_with_pcc(&fw, "\\_SB_.BAD0._GAI",
		NULL, &buf, &pcc_unsupported);
	check(status == AE_NOT_FOUND && !pcc_unsupported, "Unrelated failure inherited PCC state");
	check(!fwts_acpi_pcc_handler_called_get_and_clear(), "Unrelated failure retained PCC state");
	free(buf.Pointer);

	check(fwts_acpi_deinit(&fw) == FWTS_OK, "Cannot deinitialize ACPI");
	fwts_summary_deinit();
	check(fwts_log_close(fw.results) == FWTS_OK, "Cannot close the test log");
	return EXIT_SUCCESS;
}
