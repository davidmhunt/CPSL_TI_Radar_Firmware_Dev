/*----------------------------------------------------------------------------*/
/* Linker Settings                                                            */
--retain="*(.intvecs)"

/*----------------------------------------------------------------------------*/
/* Section Configuration                                                      */
SECTIONS
{
    systemHeap : {} > DATA_RAM
    /* SAR metadata record slots: CBUFF user buffer, EDMA-reachable L3 */
    .cbuffL3Memory : {} > L3_RAM
}
/*----------------------------------------------------------------------------*/

