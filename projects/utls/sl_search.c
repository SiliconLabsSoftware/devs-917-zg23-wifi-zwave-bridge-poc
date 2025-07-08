/*******************************************************************************
 * @file  sl_search.c
 * @brief
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/
#include "sl_search.h"

/**
 * BSearch in a sorted list
 *
 * Complexity: O(log(N))
 *
 * \param key         What to find
 * \param sorted_list numerical sorted list
 * \param list_len    Length of the list
 * \return true if element is found
 */
int b_search(uint16_t key, const int sorted_list[], int list_len)
{
  int first, last, middle;

  first = 0;
  last = list_len - 1;
  middle = (first + last) / 2;

  while (first <= last) {
    if (sorted_list[middle] < key) {
      first = middle + 1;
    } else if (sorted_list[middle] == key) {
      return 1;
    } else {
      last = middle  - 1;
    }
    middle = (first + last) / 2;
  }

  return 0;
}
