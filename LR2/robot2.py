import json
import os
import re
import signal
import sys
import time
import xml.etree.ElementTree as ET
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple
from urllib.parse import urlparse

import requests
import yaml
from pymongo import MongoClient


class CrawlerConfig:
    """Управление конфигурацией краулера"""
    
    def __init__(self, config_path: str):
        self.config_path = config_path
        self.data = self._load_config()
    
    def _load_config(self) -> Dict[str, Any]:
        try:
            with open(self.config_path, 'r', encoding='utf-8') as f:
                return yaml.safe_load(f)
        except Exception as e:
            print(f"Ошибка загрузки конфигурации: {e}")
            raise
    
    @property
    def connection_string(self) -> str:
        return self.data['db']['connection_string']
    
    @property
    def documents_database(self) -> str:
        return self.data['db']['documents_database']
    
    @property
    def state_dir(self) -> str:
        return self.data.get('state', {}).get('state_dir', 'crawler_state')
    
    @property
    def delay_between_requests(self) -> float:
        return self.data['logic']['delay_between_requests']
    
    @property
    def sitemaps(self) -> List[str]:
        return self.data['logic']['sitemaps']
    
    @property
    def max_urls_per_sitemap(self) -> int:
        return self.data['logic'].get('max_urls_per_sitemap', 0)
    
    @property
    def interval_seconds(self) -> int:
        return self.data['logic'].get('interval_seconds', 86400)
    
    @property
    def url_transforms(self) -> List[Dict[str, str]]:
        return self.data['logic'].get('url_transforms', [])


class URLTransformer:    
    def __init__(self, transform_rules: List[Dict[str, str]]):
        self.rules = self._compile_rules(transform_rules)
    
    def _compile_rules(self, transform_rules: List[Dict[str, str]]) -> List[Dict[str, Any]]:
        compiled_rules = []
        
        for rule in transform_rules:
            try:
                compiled_pattern = re.compile(rule['pattern'])
                compiled_rules.append({
                    'sitemap_pattern': rule['sitemap_pattern'],
                    'pattern': rule['pattern'],
                    'replacement': rule['replacement'],
                    'compiled_pattern': compiled_pattern
                })
                print(f"Загружено правило: {rule['sitemap_pattern']} -> {rule['pattern']}")
            except re.error as e:
                print(f"Ошибка компиляции regex '{rule['pattern']}': {e}")
                compiled_rules.append({
                    'sitemap_pattern': rule['sitemap_pattern'],
                    'pattern': rule['pattern'],
                    'replacement': rule['replacement'],
                    'compiled_pattern': None
                })
        
        return compiled_rules
    
    def transform(self, original_url: str, sitemap_url: str) -> str:
        if not self.rules:
            return original_url
        
        for rule in self.rules:
            if rule['compiled_pattern'] is None:
                continue
            
            if sitemap_url.startswith(rule['sitemap_pattern'].replace('*', '')):
                compiled_pattern = rule['compiled_pattern']
                replacement = rule['replacement']
                
                if compiled_pattern.match(original_url):
                    transformed_url = compiled_pattern.sub(replacement, original_url)
                    if transformed_url != original_url:
                        print(f"  Трансформация URL: {original_url} -> {transformed_url}")
                    return transformed_url
        
        return original_url


class SitemapParser:
    """Парсинг sitemap файлов"""
    
    NAMESPACE = {'ns': 'http://www.sitemaps.org/schemas/sitemap/0.9'}
    
    def __init__(self, timeout: int = 10):
        self.timeout = timeout
    
    def parse(self, sitemap_url: str, max_urls: int = 0) -> List[Dict[str, str]]:
        try:
            print(f"Парсинг sitemap: {sitemap_url}")
            response = requests.get(sitemap_url, timeout=self.timeout)
            response.raise_for_status()
            
            url_data_list = []
            root = ET.fromstring(response.content)
            url_elements = root.findall('ns:url', self.NAMESPACE)
            
            if max_urls > 0:
                total_urls = len(url_elements)
                url_elements = url_elements[:max_urls]
                print(f"Ограничение: обрабатывается {len(url_elements)} URL из {total_urls}")
            
            for url_element in url_elements:
                url_data = self._extract_url_data(url_element)
                if url_data:
                    url_data_list.append(url_data)
            
            print(f"Найдено {len(url_data_list)} URL в sitemap")
            return url_data_list
            
        except Exception as e:
            print(f"Ошибка парсинга sitemap {sitemap_url}: {e}")
            return []
    
    def _extract_url_data(self, url_element) -> Optional[Dict[str, str]]:
        loc = url_element.find('ns:loc', self.NAMESPACE)
        lastmod = url_element.find('ns:lastmod', self.NAMESPACE)
        
        if loc is not None and loc.text and lastmod is not None and lastmod.text:
            return {
                'url': loc.text,
                'lastmod': lastmod.text
            }
        return None


class StateManager:
    """Управление состоянием краулера"""
    
    def __init__(self, state_dir: str):
        self.state_dir = Path(state_dir)
        self.state_dir.mkdir(parents=True, exist_ok=True)
        
        self.queue_file = self.state_dir / 'queue.json'
        self.sitemap_cache_file = self.state_dir / 'sitemap_cache.json'
    
    def load_queue(self) -> List[str]:
        return self._load_json_file(self.queue_file, [])
    
    def save_queue(self, queue: List[str]) -> None:
        self._save_json_file(self.queue_file, queue)
    
    def load_sitemap_cache(self) -> Dict[str, str]:
        return self._load_json_file(self.sitemap_cache_file, {})
    
    def save_sitemap_cache(self, cache: Dict[str, str]) -> None:
        self._save_json_file(self.sitemap_cache_file, cache)
    
    def _load_json_file(self, filepath: Path, default: Any) -> Any:
        try:
            if filepath.exists():
                with open(filepath, 'r', encoding='utf-8') as f:
                    return json.load(f)
        except Exception as e:
            print(f"Ошибка загрузки файла {filepath}: {e}")
        return default
    
    def _save_json_file(self, filepath: Path, data: Any) -> None:
        try:
            with open(filepath, 'w', encoding='utf-8') as f:
                json.dump(data, f, indent=2, ensure_ascii=False)
        except Exception as e:
            print(f"Ошибка сохранения файла {filepath}: {e}")


class DocumentManager:
    """Управление документами в MongoDB"""
    
    def __init__(self, connection_string: str, database_name: str):
        self.client = MongoClient(connection_string)
        self.db = self.client[database_name]
        self.collection = self.db.documents
        self._ensure_indexes()
        self._init_id_counter()
    
    def _ensure_indexes(self) -> None:
        self.collection.create_index([("url", 1)], unique=True)
    
    def _init_id_counter(self) -> None:
        last_doc = self.collection.find_one(sort=[("_id", -1)])
        if last_doc and "_id" in last_doc:
            self.next_id = last_doc["_id"] + 1
        else:
            self.next_id = 1
    
    def get_document(self, url: str) -> Optional[Dict[str, Any]]:
        return self.collection.find_one({'url': url})
    
    def get_next_id(self) -> int:
        current_id = self.next_id
        self.next_id += 1
        return current_id
    
    def save_document(self, document: Dict[str, Any]) -> None:
        self.collection.replace_one(
            {'_id': document['_id']},
            document,
            upsert=True
        )
    
    def close(self) -> None:
        self.client.close()


class CrawlHandler:
    """Логика краулинга"""
    
    def __init__(self, delay: float):
        self.delay = delay
    
    @staticmethod
    def download_url(url: str) -> Tuple[Optional[str], Optional[int]]:
        """Скачивание страницы"""
        try:
            response = requests.get(url, timeout=10)
            response.raise_for_status()
            return response.text, response.status_code
        except Exception as e:
            print(f"Ошибка загрузки {url}: {e}")
            return None, None
    
    @staticmethod
    def get_source_name(url: str) -> str:
        """Получение названия источника из домена"""
        return urlparse(url).netloc
    
    @staticmethod
    def parse_lastmod(lastmod_string: str) -> Optional[int]:
        """Парсинг даты из lastmod в Unix timestamp"""
        try:
            formats = [
                "%Y-%m-%dT%H:%M:%S%z",
                "%Y-%m-%dT%H:%M:%S", 
                "%Y-%m-%d",
            ]
            
            for fmt in formats:
                try:
                    dt = datetime.strptime(lastmod_string, fmt)
                    return int(dt.timestamp())
                except ValueError:
                    continue
            return None
        except Exception:
            return None
    
    def needs_crawl(self, url: str, 
                    document_manager: DocumentManager,
                    sitemap_cache: Dict[str, str],
                    interval_seconds: int) -> bool:
        """
        Переобкачиваем ТОЛЬКО если:

        1. Прошел временной интервал с последней обкачки
        2. Lastmod в sitemap новее времени последней обкачки
        """
        existing_doc = document_manager.get_document(url)
        
        if not existing_doc:
            print(f"  Новый URL, требуется обкачка: {url}")
            return True
        
        current_time = time.time()
        last_crawl_time = existing_doc.get('crawl_time', 0)
        time_since_last_crawl = current_time - last_crawl_time
        time_condition = time_since_last_crawl >= interval_seconds
        
        lastmod_condition = False
        current_lastmod = sitemap_cache.get(url)
        
        if current_lastmod:
            current_lastmod_timestamp = self.parse_lastmod(current_lastmod)
            
            if current_lastmod_timestamp and last_crawl_time:
                lastmod_condition = current_lastmod_timestamp > last_crawl_time
                if lastmod_condition:
                    print(f"  Страница изменена после последней обкачки: {current_lastmod_timestamp} > {last_crawl_time}")
                else:
                    print(f"  Страница не изменялась: {current_lastmod_timestamp} <= {last_crawl_time}")
            else:
                print(f"  Не удалось сравнить lastmod: {current_lastmod_timestamp}, crawl_time: {last_crawl_time}")
        else:
            print(f"  Lastmod не найден в sitemap кэше")
        
        needs_crawl = time_condition and lastmod_condition
        
        if needs_crawl:
            print(f"  Требуется переобкачка URL: {url}")
            print(f"    Время с последней обкачки: {time_since_last_crawl:.0f} сек")
            print(f"    Lastmod новее чем обкачка: {lastmod_condition}")
        else:
            print(f"  Пропуск URL: {url}")
            print(f"    Интервал прошел: {time_condition}")
            print(f"    Lastmod новее: {lastmod_condition}")
        
        return needs_crawl


class SitemapCrawler:
    """Координатор"""
    
    def __init__(self, config_path: str):
        self.config = CrawlerConfig(config_path)
        self.url_transformer = URLTransformer(self.config.url_transforms)
        self.sitemap_parser = SitemapParser()
        self.state_manager = StateManager(self.config.state_dir)
        self.document_manager = DocumentManager(
            self.config.connection_string,
            self.config.documents_database
        )
        self.crawl_handler = CrawlHandler(self.config.delay_between_requests)
        
        self.queue = self.state_manager.load_queue()
        self.sitemap_cache = self.state_manager.load_sitemap_cache()
        
        self.running = True
        
        signal.signal(signal.SIGINT, self.signal_handler)
        signal.signal(signal.SIGTERM, self.signal_handler)
        
        print(f"Загружено задач в очередь: {len(self.queue)}")
        print(f"Загружено URL в кэше sitemap: {len(self.sitemap_cache)}")
    
    def signal_handler(self, signum: int, frame: Any) -> None:
        self.running = False
        self.save_state()
    
    def save_state(self) -> None:
        self.state_manager.save_queue(self.queue)
        self.state_manager.save_sitemap_cache(self.sitemap_cache)
        print("Состояние успешно сохранено")
    
    def refresh_sitemap_data(self) -> int:
        total_urls = 0
        
        for sitemap_url in self.config.sitemaps:
            if not self.running:
                break
                
            url_data_list = self.sitemap_parser.parse(
                sitemap_url,
                self.config.max_urls_per_sitemap
            )
            
            for url_data in url_data_list:
                final_url = self.url_transformer.transform(
                    url_data['url'],
                    sitemap_url
                )
                self.sitemap_cache[final_url] = url_data['lastmod']
                total_urls += 1
            
            self.state_manager.save_sitemap_cache(self.sitemap_cache)
        
        print(f"Обновлено данных для {total_urls} URL в JSON кэше")
        return total_urls
    
    def add_to_queue(self, url: str) -> bool:
        if url in self.queue:
            print(f"Уже в очереди: {url}")
            return False

        if not self.crawl_handler.needs_crawl(
            url, 
            self.document_manager,
            self.sitemap_cache,
            self.config.interval_seconds
        ):
            print(f"Пропуск (без изменений): {url}")
            return False
        
        self.queue.append(url)
        print(f"Добавлено в очередь: {url}")
        
        self.state_manager.save_queue(self.queue)
        return True
    
    def get_from_queue(self) -> Optional[str]:
        if self.queue:
            url = self.queue.pop(0)
            self.state_manager.save_queue(self.queue)
            return url
        return None
    
    def fill_queue_from_sitemap_data(self) -> int:
        added_count = 0
        
        for url in self.sitemap_cache.keys():
            if not self.running:
                break
                
            if self.add_to_queue(url):
                added_count += 1
        
        print(f"Добавлено в очередь: {added_count} URL")
        return added_count
    
    def process_url(self, url: str) -> bool:
        print(f"Обработка: {url}")
        
        html, status_code = self.crawl_handler.download_url(url)
        
        if not html:
            print(f"  Ошибка загрузки")
            return False
        
        source_name = self.crawl_handler.get_source_name(url)
        crawl_time = time.time()
        
        existing_doc = self.document_manager.get_document(url)
        
        if existing_doc:
            doc_id = existing_doc['_id']
            print(f"  Переобкачка документа (ID: {doc_id})")
        else:
            doc_id = self.document_manager.get_next_id()
            print(f"  Первая обкачка документа (ID: {doc_id})")
        
        document = {
            '_id': doc_id,
            'url': url,
            'html': html,
            'source_name': source_name,
            'crawl_time': crawl_time
        }
        
        self.document_manager.save_document(document)
        print(f"  Успешно сохранено в БД (ID: {doc_id})")
        return True
    
    def process_queue(self) -> int:
        processed_count = 0
        
        while self.running and self.queue:
            url = self.get_from_queue()
            
            if not url:
                break
            
            success = self.process_url(url)
            if success:
                processed_count += 1
            
            if self.running:
                time.sleep(self.config.delay_between_requests)
        
        return processed_count
    
    def run(self) -> None:
        print(f"Основная БД: {self.config.documents_database}")
        print(f"Директория состояния: {self.config.state_dir}")
        print(f"Задержка между запросами: {self.config.delay_between_requests} секунд")
        
        max_urls_display = (
            'без ограничений' 
            if self.config.max_urls_per_sitemap <= 0 
            else self.config.max_urls_per_sitemap
        )
        print(f"Максимум URL из sitemap: {max_urls_display}")
        
        if self.config.url_transforms:
            print(f"Загружено правил трансформации URL: {len(self.config.url_transforms)}")
        
        try:
            cycle_count = 0
            while self.running:
                cycle_count += 1
                print(f"\n" + "="*50)
                print(f"Цикл #{cycle_count} - {time.strftime('%Y-%m-%d %H:%M:%S')}")
                
                if len(self.queue) == 0:
                    self.refresh_sitemap_data()
                
                queue_size = len(self.queue)
                print(f"Задач в очереди: {queue_size}")
                
                if queue_size == 0:
                    added = self.fill_queue_from_sitemap_data()
                    if added == 0:
                        print("Изменений не обнаружено, ожидание...")
                        for _ in range(60):
                            if not self.running:
                                break
                            time.sleep(10)
                        continue
                
                processed = self.process_queue()
                print(f"Обработано URL: {processed}")
                
                self.save_state()
                
        except Exception as e:
            print(f"Ошибка в основном цикле: {e}")
        finally:
            self.save_state()
            self.document_manager.close()
            print("Робот остановлен")


def main() -> None:
    if len(sys.argv) != 2:
        print("Использование: python sitemap_crawler.py config.yaml")
        sys.exit(1)
    
    crawler = SitemapCrawler(sys.argv[1])
    crawler.run()


if __name__ == "__main__":
    main()