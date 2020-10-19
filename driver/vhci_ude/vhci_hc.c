#include "vhci_driver.h"
#include "vhci_hc.tmh"

#define MAX_HUB_PORTS		4

#include "usbip_vhci_api.h"

extern VOID create_queue_hc(pctx_vhci_t vhci);

static NTSTATUS
controller_query_usb_capability(WDFDEVICE UdecxWdfDevice, PGUID CapabilityType,
	ULONG OutputBufferLength, PVOID OutputBuffer, PULONG ResultLength)
{
	UNREFERENCED_PARAMETER(UdecxWdfDevice);
	UNREFERENCED_PARAMETER(CapabilityType);
	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(OutputBuffer);
	UNREFERENCED_PARAMETER(ResultLength);

	TRW(VHCI, "not supported: %!GUID!", CapabilityType);

	return STATUS_NOT_SUPPORTED;
}

static VOID
controller_reset(WDFDEVICE UdecxWdfDevice)
{
	UNREFERENCED_PARAMETER(UdecxWdfDevice);

	TRW(VHCI, "Enter");
}

static PAGEABLE BOOLEAN
create_ucx_controller(WDFDEVICE hdev)
{
	UDECX_WDF_DEVICE_CONFIG	conf;
	NTSTATUS	status;

	UDECX_WDF_DEVICE_CONFIG_INIT(&conf, controller_query_usb_capability);
	conf.EvtUdecxWdfDeviceReset = controller_reset;
	/* conf.NumberOfUsb30Ports=1 by UDECX_WDF_DEVICE_CONFIG_INIT */
	conf.NumberOfUsb20Ports = MAX_HUB_PORTS;
	/* UdecxWdfDeviceAddUsbDeviceEmulation() will fail if NumberOfUsb20Ports or NumberOfUsb30Ports is 0 */
	status = UdecxWdfDeviceAddUsbDeviceEmulation(hdev, &conf);
	if (NT_ERROR(status)) {
		TRE(VHCI, "failed to create controller: %!STATUS!", status);
		return FALSE;
	}

	return TRUE;
}

static PAGEABLE VOID
setup_fileobject(PWDFDEVICE_INIT dinit)
{
	WDF_OBJECT_ATTRIBUTES	attrs;
	WDF_FILEOBJECT_CONFIG	conf;

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, pctx_vusb_t);
	WDF_FILEOBJECT_CONFIG_INIT(&conf, NULL, NULL, NULL);
	WdfDeviceInitSetFileObjectConfig(dinit, &conf, &attrs);
}

static PAGEABLE VOID
reg_devintf(WDFDEVICE hdev)
{
	NTSTATUS	status;

	status = WdfDeviceCreateDeviceInterface(hdev, &GUID_DEVINTERFACE_VHCI_USBIP, NULL);
	if (NT_ERROR(status)) {
		TRE(VHCI, "failed to register usbip device interface: %!STATUS!", status);
	}
	status = WdfDeviceCreateDeviceInterface(hdev, &GUID_DEVINTERFACE_USB_HOST_CONTROLLER, NULL);
	if (NT_ERROR(status)) {
		TRE(VHCI, "failed to register host controller device interface: %!STATUS!", status);
	}
}

static BOOLEAN
setup_vhci(pctx_vhci_t vhci)
{
	WDF_OBJECT_ATTRIBUTES       attrs;
	NTSTATUS	status;

	WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
	attrs.ParentObject = vhci->hdev;
	status = WdfWaitLockCreate(&attrs, &vhci->lock);
	if (NT_ERROR(status)) {
		TRE(VHCI, "failed to create wait lock: %!STATUS!", status);
		return FALSE;
	}
	vhci->n_max_ports = MAX_HUB_PORTS;

	vhci->vusbs = ExAllocatePoolWithTag(PagedPool, sizeof(pctx_vusb_t) * vhci->n_max_ports, VHCI_POOLTAG);
	if (vhci->vusbs == NULL) {
		TRE(VHCI, "failed to allocate ports: out of memory");
		return FALSE;
	}
	RtlZeroMemory(vhci->vusbs, sizeof(pctx_vusb_t) * vhci->n_max_ports);

	return TRUE;
}

PAGEABLE NTSTATUS
evt_add_vhci(_In_ WDFDRIVER drv, _Inout_ PWDFDEVICE_INIT dinit)
{
	pctx_vhci_t	vhci;
	WDFDEVICE	hdev;
	WDF_OBJECT_ATTRIBUTES       attrs;
	NTSTATUS	status;

	UNREFERENCED_PARAMETER(drv);

	PAGED_CODE();

	TRD(VHCI, "Enter");

	status = UdecxInitializeWdfDeviceInit(dinit);
	if (!NT_SUCCESS(status)) {
		TRE(VHCI, "failed to initialize UDE: %!STATUS!", status);
		goto out;
	}

	setup_fileobject(dinit);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, ctx_vhci_t);
	status = WdfDeviceCreate(&dinit, &attrs, &hdev);
	if (!NT_SUCCESS(status)) {
		TRE(VHCI, "failed to create wdf device: %!STATUS!", status);
		goto out;
	}

	if (!create_ucx_controller(hdev)) {
		status = STATUS_UNSUCCESSFUL;
		goto out;
	}

	reg_devintf(hdev);

	vhci = TO_VHCI(hdev);
	vhci->hdev = hdev;
	if (!setup_vhci(vhci))
		goto out;

	create_queue_hc(vhci);

	status = STATUS_SUCCESS;
out:
	TRD(VHCI, "Leave: %!STATUS!", status);

	return status;
}