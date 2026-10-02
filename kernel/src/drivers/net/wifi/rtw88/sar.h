#include "main.h"
#define RTW_COMMON_SAR_FCT 2
struct rtw_sar_arg {
 u8 sar_band;
 u8 path;
 u8 rs;
};
extern const struct cfg80211_sar_capa rtw_sar_capa;
s8 rtw_query_sar(struct rtw_dev *rtwdev, const struct rtw_sar_arg *arg);
int rtw_set_sar_specs(struct rtw_dev *rtwdev,
        const struct cfg80211_sar_specs *sar);
