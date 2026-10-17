from prepare_control_button import add_button
from pathlib import Path
import copy
source={'Controllers':[{'Type':'Keypad','Actions':{'0,0':{'UUID':'back'},'1,0':{'UUID':'white'}}},{'Type':'Other','data':[1,2]}],'Name':'Lights'}
before=copy.deepcopy(source)
result=add_button(source,Path('principal.lnk'))
assert source==before and result['Controllers'][1]==source['Controllers'][1]
for key,value in source['Controllers'][0]['Actions'].items():assert result['Controllers'][0]['Actions'][key]==value
assert set(result['Controllers'][0]['Actions'])=={'0,0','1,0','0,2'}
try:add_button(result,Path('other.lnk'))
except ValueError:pass
else:raise AssertionError('Must not overwrite existing button')
print('PASS existing actions/navigation/controllers preserved, occupied slot rejected')
