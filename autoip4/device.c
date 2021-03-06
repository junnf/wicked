/*
 * An IPv4LL RFC 3927 supplicant for wicked
 *
 * Copyright (C) 2010-2012 Olaf Kirch <okir@suse.de>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/poll.h>
#include <net/if_arp.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include <wicked/netinfo.h>
#include <wicked/addrconf.h>
#include <wicked/logging.h>
#include "netinfo_priv.h"
#include "autoip.h"
#include "appconfig.h"

ni_autoip_device_t *	ni_autoip_active;

/*
 * Create and destroy autoip device handles
 */
ni_autoip_device_t *
ni_autoip_device_new(const char *ifname, const ni_linkinfo_t *link)
{
	ni_autoip_device_t *dev, **pos;

	for (pos = &ni_autoip_active; (dev = *pos) != NULL; pos = &dev->next)
		;

	dev = calloc(1, sizeof(*dev));
	ni_string_dup(&dev->ifname, ifname);
	dev->link.ifindex = link->ifindex;
	dev->users = 1;
	dev->fsm.state = NI_AUTOIP_STATE_INIT;

	if (ni_capture_devinfo_init(&dev->devinfo, ifname, link) < 0) {
		ni_autoip_device_put(dev);
		return NULL;
	}

	/* append to end of list */
	*pos = dev;

	return dev;
}

ni_autoip_device_t *
ni_autoip_device_find(const char *ifname)
{
	ni_autoip_device_t *dev;

	for (dev = ni_autoip_active; dev; dev = dev->next) {
		if (!strcmp(dev->ifname, ifname))
			return dev;
	}

	return NULL;
}

ni_autoip_device_t *
ni_autoip_device_by_index(unsigned int ifindex)
{
	ni_autoip_device_t *dev;

	for (dev = ni_autoip_active; dev; dev = dev->next) {
		if (dev->link.ifindex == ifindex)
			return dev;
	}

	return NULL;
}

static void
ni_autoip_device_close(ni_autoip_device_t *dev)
{
	if (dev->arp_socket)
		ni_arp_socket_close(dev->arp_socket);
	dev->arp_socket = NULL;

	if (dev->fsm.timer) {
		ni_timer_cancel(dev->fsm.timer);
		dev->fsm.timer = NULL;
	}
}

void
ni_autoip_device_stop(ni_autoip_device_t *dev)
{
	/* Clear the lease. This will trigger an event to wickedd
	 * with a lease that has state RELEASED. */
	ni_autoip_device_drop_lease(dev);
	ni_autoip_device_close(dev);
}

void
ni_autoip_device_set_lease(ni_autoip_device_t *dev, ni_addrconf_lease_t *lease)
{
	if (dev->lease != lease) {
		if (dev->lease)
			ni_addrconf_lease_free(dev->lease);
		dev->lease = lease;
	}
}

void
ni_autoip_device_drop_lease(ni_autoip_device_t *dev)
{
	ni_addrconf_lease_t *lease;

	if ((lease = dev->lease) != NULL) {
		/* if we've configured the network using this
		 * lease, we need to isse a link down request */
		dev->notify = 1;

		/* delete the lease file. */
		ni_addrconf_lease_file_remove(dev->ifname, lease->type, lease->family);
		ni_autoip_device_set_lease(dev, NULL);

		/* Go back to square one */
		dev->fsm.state = NI_AUTOIP_STATE_INIT;
	}
}

void
ni_autoip_device_free(ni_autoip_device_t *dev)
{
	ni_autoip_device_t **pos;

	ni_assert(dev->users == 0);
	ni_debug_autoip("%s: Deleting autoip4 device with index %u",
			dev->ifname, dev->link.ifindex);

	ni_autoip_device_drop_lease(dev);
	ni_autoip_device_close(dev);

	ni_string_free(&dev->devinfo.ifname);
	ni_string_free(&dev->ifname);
	dev->link.ifindex = 0;

	for (pos = &ni_autoip_active; *pos; pos = &(*pos)->next) {
		if (*pos == dev) {
			*pos = dev->next;
			break;
		}
	}
	free(dev);
}

ni_autoip_device_t *
ni_autoip_device_get(ni_autoip_device_t *dev)
{
	ni_assert(dev->users);
	dev->users++;
	return dev;
}

void
ni_autoip_device_put(ni_autoip_device_t *dev)
{
	ni_assert(dev->users);
	if (--(dev->users) == 0)
		ni_autoip_device_free(dev);
}

/*
 * Process a request to reconfigure the device (ie rebind a lease, or discover
 * a new lease).
 */
int
ni_autoip_device_refresh(ni_autoip_device_t *dev)
{
	ni_netconfig_t *nih = ni_global_state_handle(0);
	int rv;

	/* Go back to INIT state to force a reclaim */
	dev->fsm.state = NI_AUTOIP_STATE_INIT;

	if ((rv = __ni_device_refresh_link_info(nih, &dev->link)) < 0) {
		ni_error("%s: cannot refresh interface: %s", __func__, ni_strerror(rv));
		return rv;
	}

	return ni_capture_devinfo_refresh(&dev->devinfo, dev->ifname, &dev->link);
}

int
ni_autoip_device_start(ni_autoip_device_t *dev)
{
	ni_autoip_device_drop_lease(dev);
	dev->failed = 0;

	if (ni_autoip_fsm_select(dev) < 0) {
		ni_error("%s: unable to initiate discovery", dev->ifname);
		return -1;
	}

	return 0;
}

/*
 * Acquire an IPv4ll lease
 */
int
ni_autoip_acquire(ni_autoip_device_t *dev)
{
	return ni_autoip_device_start(dev);
}

int
ni_autoip_release(ni_autoip_device_t *dev, const ni_uuid_t *uuid)
{
	ni_autoip_device_stop(dev);
	return 0;
}
