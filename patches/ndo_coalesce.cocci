@@
parameter P1, P2, P3, P4;
@@

static int gve_get_coalesce( P1, P2
+#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0) || (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(8,4) && RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(9,0)) || RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(9,2)
 ,P3, P4
+#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0) || (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(8,4) && RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(9,0)) || RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(9,2) */
 )
{
...
}

@@
parameter P1, P2, P3, P4;
@@
static int gve_set_coalesce( P1, P2
+#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0) || (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(8,4) && RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(9,0)) || RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(9,2)
 ,P3, P4
+#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0) || (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(8,4) && RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(9,0)) || RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(9,2) */
 )
{
...
}
