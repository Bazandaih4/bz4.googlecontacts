/**
 * @file bz4.googlecontacts
 * @brief Конвертирует CSV файл из Google Forms в формат Google Contacts CSV.
 *
 * Программа читает CSV файл, выгруженный из Google Forms (с определенной структурой),
 * извлекает данные о контактах (Имя, Фамилия, Группа, Email, Телефон, Должность)
 * и создает новый CSV файл, готовый для импорта в Google Contacts.
 *
 * Входной CSV файл должен иметь следующую структуру столбцов (порядок важен):
 * 0: Отметка времени (не используется)
 * 1: Должность
 * 2: Имя с большой буквы
 * 3: Группа, Фамилия и подчеркивание (например, "ПМ-35 ПОНОМАРЕВ")
 * 4: Почта 1 (логин от личного кабинета)
 * 5: Почта 2 (созданная почта)
 * 6: Номер телефона, начиная с +7
 *
 * Выходной CSV файл:
 * - Формат: Google Contacts CSV (23 столбца).
 * - Кодировка: UTF-8 с BOM (Byte Order Mark).
 * - Имя файла: output_contacts_utf8.csv (по умолчанию) или указанное пользователем.
 * - Заполняемые поля: First Name, Last Name (как "Группа Фамилия"), Labels (задается пользователем),
 *   E-mail 1 Value (созданная почта), E-mail 2 Value (почта ЛК), Phone 1 Value.
 * - Поля Organization Name, Organization Title, E-mail Labels, Phone Label НЕ ЗАПОЛНЯЮТСЯ.
 *
 * Компиляция (пример с g++):
 *   g++ bz4.googlecontacts -o csv_transformer -std=c++11 (или новее)
 *
 * Использование:
 * 1. Поместите исходный CSV файл в ту же директорию, что и скомпилированная программа.
 * 2. Запустите программу: ./csv_transformer
 * 3. Или укажите имена файлов: ./csv_transformer "input.csv" "output.csv"
 * 4. Программа запросит название для группы контактов (Labels).
 * 5. Будет создан выходной CSV файл.
 *
 * Примечание для Windows: Для корректного отображения/ввода кириллицы в консоли
 * может потребоваться выполнить команду 'chcp 1251' перед запуском программы
 * и использовать шрифт консоли, поддерживающий кириллицу (Consolas, Lucida Console).
 */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <stdexcept> // Для std::runtime_error
#include <algorithm> // Для std::max
#include <vector>    // Убедимся, что vector включен
#include <windows.h> // Для SetConsoleCP/SetConsoleOutputCP
#include <locale>    // Для setlocale

 // --- Вспомогательные функции ---

 /**
  * @brief Разбирает строку CSV на отдельные поля с учетом кавычек.
  *
  * Поддерживает поля, заключенные в двойные кавычки, и экранированные
  * двойные кавычки ("") внутри таких полей.
  *
  * @param line Строка CSV для разбора.
  * @return std::vector<std::string> Вектор строк, содержащий поля из строки CSV.
  */
std::vector<std::string> parse_csv_line(const std::string &line)
{
   std::vector<std::string> fields;
   std::string field_buffer;
   bool in_quotes = false;
   std::stringstream ss(line);
   char c;

   while (ss.get(c))
   {
      if (c == '"')
      {
         if (ss.peek() == '"')
         {
            // Двойная кавычка "" внутри поля
            field_buffer += '"';
            ss.get(); // Пропускаем вторую кавычку
         }
         else
         {
            // Одиночная кавычка - начало или конец поля в кавычках
            in_quotes = !in_quotes;
         }
      }
      else if (c == ',' && !in_quotes)
      {
         // Запятая-разделитель вне кавычек
         fields.push_back(field_buffer);
         field_buffer.clear();
      }
      else
      {
         // Обычный символ
         field_buffer += c;
      }
   }
   // Добавляем последнее поле (после последней запятой или если запятых не было)
   fields.push_back(field_buffer);
   return fields;
}

/**
 * @brief Форматирует поле для безопасной записи в CSV.
 *
 * Если поле содержит запятую, кавычку или символ новой строки,
 * оно заключается в двойные кавычки, а внутренние двойные кавычки
 * удваиваются ("").
 *
 * @param field Строка (поле) для форматирования.
 * @return std::string Отформатированная строка, готовая к записи в CSV.
 */
std::string format_csv_field(const std::string &field)
{
   // Проверяем, нужно ли экранирование
   if (field.find(',') != std::string::npos || field.find('"') != std::string::npos || field.find('\n') != std::string::npos)
   {
      std::string escaped_field = "\"";
      for (char c : field)
      {
         if (c == '"')
         {
            // Экранируем кавычки двойными кавычками
            escaped_field += "\"\"";
         }
         else
         {
            escaped_field += c;
         }
      }
      escaped_field += "\"";
      return escaped_field;
   }
   // Если экранирование не требуется, возвращаем поле как есть
   return field;
}

/**
 * @brief Разделяет комбинированную строку "Группа Фамилия" на две части.
 *
 * Ищет первый пробел как разделитель. Всё до первого пробела считается группой,
 * всё после - фамилией. Учитывает возможные лишние пробелы.
 *
 * @param combined Входная строка (например, "ПМ-35 ПОНОМАРЕВ").
 * @param group Выходной параметр для сохранения группы (например, "ПМ-35").
 * @param lastName Выходной параметр для сохранения фамилии (например, "ПОНОМАРЕВ").
 */
void splitGroupLastName(const std::string &combined, std::string &group, std::string &lastName)
{
   group = "";
   lastName = "";
   if (combined.empty())
   {
      return;
   }

   size_t first_space = combined.find(' ');
   if (first_space != std::string::npos)
   {
      // Нашли пробел - разделяем
      group = combined.substr(0, first_space);
      // Убираем возможные пробелы в конце извлеченной группы
      size_t last_char_group = group.find_last_not_of(' ');
      if (last_char_group != std::string::npos)
      {
         group.erase(last_char_group + 1);
      }
      else
      {
         // Если вся "группа" состояла из пробелов
         group.clear();
      }

      // Ищем начало фамилии (первый непробельный символ после первого пробела)
      size_t last_name_start = combined.find_first_not_of(' ', first_space);
      if (last_name_start != std::string::npos)
      {
         lastName = combined.substr(last_name_start);
      }
      // Если после первого пробела были только пробелы, lastName останется ""
   }
   else
   {
      // Пробел не найден, считаем всю строку фамилией
      lastName = combined;
   }
}


// --- Основная логика ---

int main(int argc, char *argv[])
{
   // --- Настройка локали и кодировки консоли (для Windows) ---
   try
   {
      setlocale(LC_ALL, ""); // Устанавливаем системную локаль по умолчанию
   }
   catch (const std::exception &e)
   {
      // Не критичная ошибка, выводим предупреждение
      std::cerr << "Предупреждение: Не удалось установить локаль. " << e.what() << std::endl;
   }
   // Установка кодовых страниц для консоли Windows (1251 для кириллицы)
   if (!SetConsoleOutputCP(1251))
   {
      std::cerr << "Предупреждение: Не удалось установить код. стр. вывода 1251. Ошибка: " << GetLastError() << std::endl;
   }
   if (!SetConsoleCP(1251))
   {
      std::cerr << "Предупреждение: Не удалось установить код. стр. ввода 1251. Ошибка: " << GetLastError() << std::endl;
   }
   // --- Конец настройки ---

   // --- Определение имен входного и выходного файлов ---
   std::string input_filename = "input.csv"; // Имя входного файла по умолчанию
   std::string output_filename = "output.csv"; // Имя выходного файла по умолчанию

   // Обработка аргументов командной строки для переопределения имен файлов
   if (argc == 3)
   {
      // Если передано два аргумента (помимо имени программы)
      input_filename = argv[1];
      output_filename = argv[2];
   }
   else if (argc != 1)
   {
      // Если количество аргументов не 0 и не 2
      std::cerr << "Ошибка: Неверное количество аргументов." << std::endl;
      std::cerr << "Использование: " << argv[0] << " [\"путь/к/входному файлу.csv\"] [\"путь/к/выходному файлу.csv\"]" << std::endl;
      std::cerr << "Примечание: Используйте кавычки, если пути содержат пробелы." << std::endl;
      return 1; // Выход с кодом ошибки
   }

   std::cout << "Чтение из файла: " << input_filename << std::endl;
   std::cout << "Запись в файл:   " << output_filename << " (кодировка UTF-8 с BOM)" << std::endl;

   // --- Запрос названия группы контактов (для поля Labels) ---
   std::string contact_group_label;
   std::cout << "Введите название для группы контактов (оставьте пустым, если не нужно): ";
   // Используем getline для чтения всей строки, включая пробелы
   std::getline(std::cin, contact_group_label);
   std::cout << "Используется метка группы: '" << (contact_group_label.empty() ? "[ПУСТО]" : contact_group_label) << "'" << std::endl;
   // --- Конец запроса ---


   // --- Открытие файлов ---
   // Открываем входной файл для чтения
   std::ifstream input_file(input_filename);
   if (!input_file.is_open())
   {
      std::cerr << "Ошибка: Не удалось открыть входной файл: " << input_filename << std::endl;
      return 1;
   }

   // Открываем выходной файл для записи в БИНАРНОМ режиме (важно для BOM и корректной записи UTF-8)
   std::ofstream output_file(output_filename, std::ios::binary);
   if (!output_file.is_open())
   {
      std::cerr << "Ошибка: Не удалось открыть выходной файл: " << output_filename << std::endl;
      input_file.close(); // Закрываем уже открытый входной файл перед выходом
      return 1;
   }

   // --- Подготовка выходного файла ---
   // Записываем UTF-8 BOM (Byte Order Mark) - обязательно для корректного импорта UTF-8 в некоторых программах (включая Google Contacts)
   output_file << (char)0xEF << (char)0xBB << (char)0xBF;

   // Заголовок для выходного файла (формат Google Contacts)
   const std::string output_header = "First Name,Middle Name,Last Name,Phonetic First Name,Phonetic Middle Name,Phonetic Last Name,Name Prefix,Name Suffix,Nickname,File As,Organization Name,Organization Title,Organization Department,Birthday,Notes,Photo,Labels,E-mail 1 - Label,E-mail 1 - Value,E-mail 2 - Label,E-mail 2 - Value,Phone 1 - Label,Phone 1 - Value";
   const int NUM_OUTPUT_COLUMNS = 23; // Количество столбцов в заголовке Google Contacts

   // Индексы нужных столбцов во ВХОДНОМ файле (0-based)
   const int INPUT_IDX_ROLE = 1;             // Должность (не используется для вывода)
   const int INPUT_IDX_FIRSTNAME = 2;        // Имя
   const int INPUT_IDX_GROUPLASTNAME = 3;    // Группа + Фамилия
   const int INPUT_IDX_EMAILLOGIN = 4;       // Email 1 (ЛК)
   const int INPUT_IDX_EMAILCREATED = 5;     // Email 2 (Созданный)
   const int INPUT_IDX_PHONE = 6;            // Телефон
   const int INPUT_NUM_COLUMNS_EXPECTED = 7; // Минимальное ожидаемое кол-во столбцов во входном файле

   // Записываем заголовок в выходной файл
   output_file << output_header << "\n"; // Используем '\n' для новой строки в бинарном режиме

   // --- Обработка строк входного файла ---
   std::string line;          // Буфер для прочитанной строки
   bool is_first_line = true; // Флаг для пропуска заголовка входного файла
   int line_number = 0;       // Счетчик строк для сообщений об ошибках
   int processed_count = 0;   // Счетчик успешно обработанных строк данных

   // Читаем входной файл построчно
   while (std::getline(input_file, line))
   {
      line_number++;
      if (line.empty())
      {
         // Пропускаем пустые строки
         std::cerr << "Предупреждение: Пропущена пустая строка #" << line_number << std::endl;
         continue;
      }

      if (is_first_line)
      {
         // Пропускаем первую строку (заголовок) входного файла
         is_first_line = false;
         continue;
      }

      // --- Обработка строки данных ---
      // Разбираем строку на поля
      std::vector<std::string> input_fields = parse_csv_line(line);

      // Проверяем, достаточно ли столбцов в прочитанной строке
      if (input_fields.size() < INPUT_NUM_COLUMNS_EXPECTED)
      {
         std::cerr << "Предупреждение: Строка #" << line_number << " пропущена из-за недостаточного количества столбцов ("
            << input_fields.size() << " найдено, ожидалось минимум " << INPUT_NUM_COLUMNS_EXPECTED << "). Строка: " << line << std::endl;
         continue; // Переходим к следующей строке
      }

      try
      {
         // Создаем вектор для полей ВЫХОДНОЙ строки, инициализируем пустыми строками
         std::vector<std::string> output_fields(NUM_OUTPUT_COLUMNS, "");

         // --- Заполнение полей выходной строки ---

         // 0: First Name (Имя)
         output_fields[0] = input_fields[INPUT_IDX_FIRSTNAME];

         // 1: Middle Name - остается пустым

         // Извлекаем Группу и Фамилию из соответствующего поля входного файла
         std::string group, lastName;
         splitGroupLastName(input_fields[INPUT_IDX_GROUPLASTNAME], group, lastName);

         // 2: Last Name (Фамилия) - формируем как "Группа Фамилия"
         if (!group.empty())
         {
            output_fields[2] = group + " " + lastName;
         }
         else
         {
            // Если группа не найдена, записываем только фамилию
            output_fields[2] = lastName;
         }

         // 3-9: Пусто (Phonetics, Prefix, Suffix, Nickname, File As)

         // 10: Organization Name <- НЕ ЗАПОЛНЯЕТСЯ
         // output_fields[10] = group;

         // 11: Organization Title <- НЕ ЗАПОЛНЯЕТСЯ
         // output_fields[11] = input_fields[INPUT_IDX_ROLE];

         // 12-15: Пусто (Department, Birthday, Notes, Photo)

         // 16: Labels (Метки) - значение, введенное пользователем
         output_fields[16] = contact_group_label;

         // 17: E-mail 1 - Label <- НЕ ЗАПОЛНЯЕТСЯ
         // output_fields[17] = "Work";

         // 18: E-mail 1 - Value (Созданный Email)
         output_fields[18] = input_fields[INPUT_IDX_EMAILCREATED];

         // 19: E-mail 2 - Label <- НЕ ЗАПОЛНЯЕТСЯ
         // output_fields[19] = "Other";

         // 20: E-mail 2 - Value (Email ЛК)
         output_fields[20] = input_fields[INPUT_IDX_EMAILLOGIN];

         // 21: Phone 1 - Label <- НЕ ЗАПОЛНЯЕТСЯ
         // output_fields[21] = "Mobile";

         // 22: Phone 1 - Value (Телефон)
         output_fields[22] = input_fields[INPUT_IDX_PHONE];

         // --- Форматирование и запись выходной строки ---
         for (size_t i = 0; i < output_fields.size(); ++i)
         {
            // Форматируем каждое поле перед записью (добавляем кавычки, если нужно)
            output_file << format_csv_field(output_fields[i]);
            // Добавляем запятую после каждого поля, кроме последнего
            if (i < output_fields.size() - 1)
            {
               output_file << ",";
            }
         }
         // Завершаем строку символом новой строки
         output_file << "\n"; // Используем '\n' в бинарном режиме

         // Увеличиваем счетчик успешно обработанных строк
         processed_count++;

      }
      catch (const std::out_of_range &oor)
      {
         // Обработка ошибки: попытка доступа к несуществующему индексу (маловероятно из-за проверки выше)
         std::cerr << "Ошибка: Произошел выход за пределы диапазона при обработке строки #" << line_number << ". " << oor.what() << ". Строка: " << line << std::endl;
      }
      catch (const std::exception &e)
      {
         // Обработка других возможных исключений при обработке строки
         std::cerr << "Ошибка: Произошло исключение при обработке строки #" << line_number << ". " << e.what() << ". Строка: " << line << std::endl;
      }
   } // Конец цикла while (std::getline)

   // --- Завершение работы ---
   std::cout << "Обработка завершена. Успешно обработано строк данных: " << processed_count << "." << std::endl;

   // Закрываем файлы (хотя деструкторы ifstream/ofstream сделают это автоматически при выходе из main)
   input_file.close();
   output_file.close();

   return 0;
}