# strl_robotics
STRL Robotics framework for mobile robots


1. Система координат карты проходимости (occupancy grid), строящейся методом RTabMap (Андрей Криштопик)<br/>
Occupancy grid в rtabmap представлена стандартными сообщениями ros. Подробнее про систему координат occupancy grid можно почитать в [google doc’е](https://docs.google.com/document/d/1c-a6FynTeuAUqqo1ZnpUuR1_c4Y5wkpEJcfqtCDtESY/edit?usp=sharing) в разделе “Работа с Occupancy Grid”. Также в папке occupancy\_grid\_mapping есть папка rtabmap\_example, в которой находится скрипт occupancy\_grid\_demo.py, реализующий работу с occupancy grid. Про этот скрипт написано в том же google doc'е в разделе "Реализация работы с occupancy grid".

2. Переход из системы координат карты в систему координат одометрии (в систему координат baselink робота) (Андрей Криштопик + Линар Абдразаков + Владислав Головин)<br/>
Чтобы получить трансформацию между различными системами координат, можно использовать функцию [lookupTransform](http://wiki.ros.org/tf2/Tutorials/Writing%20a%20tf2%20listener%20%28C%2B%2B%29) для C++ или [lookup_transform](http://wiki.ros.org/tf2/Tutorials/Writing%20a%20tf2%20listener%20%28Python%29) для Python. Пример получения координат робота в мировой системе координат можно посмотреть в скрипте occupancy\_grid\_demo.py в папке occupancy\_grid\_mapping/rtabmap\_example/scripts в функции get\_robot\_world\_pose.

3. Переход из системы координат baselink робота в систему координат zed-камеры (Линар Абдразаков)

4. О запуске RTabMap и построения ocupancy grid в multisession режиме (Андрей Криштопик)<br/>
В папке occupancy\_grid\_mapping есть папка rtabmap\_example, в которой находится launch файл для запуска rtabmap и вспомогательные скрипты. Про запуск rtabmap с помощью launch файла из rtabmap\_example написано в [google doc'е](https://docs.google.com/document/d/1CMNFhYlmfJb-XJJDk92J0y-mMTqQdyz_rtd2w-TJOmI/edit?usp=sharing). В этом же google doc'е описана работа в режиме multi-session.

5. О запуске комплексирования данных одометрии (Линар Абдразаков)

6. О планировании движения к цели, заданной на карте (Владислав Головин)

7. О реализации движения по траектории (Мухаммад Алхаддад)

8. О программном управлении манипулятором (Константин Миронов)

9. Об обнаружении 3D-позы манипулируемых объектов (например, кнопок лифта) (Сергей Линок)
